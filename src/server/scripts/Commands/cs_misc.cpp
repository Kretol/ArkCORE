/*
 * Copyright (C) 2005 - 2012 MaNGOS <http://www.getmangos.com/>
 *
 * Copyright (C) 2008 - 2012 Trinity <http://www.trinitycore.org/>
 *
 * Copyright (C) 2006 - 2012 ScriptDev2 <http://www.scriptdev2.com/>
 *
 * Copyright (C) 2010 - 2012 ProjectSkyfire <http://www.projectskyfire.org/>
 *
 * Copyright (C) 2011 - 2012 ArkCORE <http://www.arkania.net/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */


#include "Chat.h"
#include "ScriptMgr.h"
#include "AccountMgr.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "Group.h"
#include "InstanceSaveMgr.h"
#include "Language.h"
#include "MovementGenerator.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "SpellAuras.h"
#include "TargetedMovementGenerator.h"
#include "WeatherMgr.h"
#include "ace/INET_Addr.h"
#include "Player.h"
#include "Pet.h"

class misc_commandscript : public CommandScript
{
public:
    misc_commandscript() : CommandScript("misc_commandscript") { }

    ChatCommand* GetCommands() const
    {
        static ChatCommand commandTable[] =
        {
            { "dev",                SEC_ADMINISTRATOR,      false, &HandleDevCommand,                   "", NULL },
            // Custom stuff
            { "addrpitem",          SEC_PLAYER,             false, &HandleAddRPItemCommand,             "", NULL },
            { "taxi",               SEC_PLAYER,             false, &HandleSelfTaxiCheatCommand,         "", NULL },
            { "scale",              SEC_PLAYER,             false, &HandleSelfScaleCommand,             "", NULL },
            { "playlocal",          SEC_GAMEMASTER,         false, &HandlePlayLocalCommand,             "", NULL },
            { "morph",              SEC_GAMEMASTER,         false, &HandleSelfMorphCommand,             "", NULL },
            { "additemall",         SEC_ADMINISTRATOR,      false, &HandleAddItemAllCommand,            "", NULL },
            { "unauraall",          SEC_ADMINISTRATOR,      false, &HandleUnAuraAllCommand,             "", NULL },
            { "masssummon",         SEC_GAMEMASTER,         false, &HandleMassSummonCommand,            "", NULL },
            { NULL,                 0,                      false, NULL,                                "", NULL }
        };
        return commandTable;
    }

    static bool HandleDevCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
        {
            if (handler->GetSession()->GetPlayer()->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_DEVELOPER))
                handler->GetSession()->SendNotification(LANG_DEV_ON);
            else
                handler->GetSession()->SendNotification(LANG_DEV_OFF);
            return true;
        }

        std::string argstr = (char*)args;

        if (argstr == "on")
        {
            handler->GetSession()->GetPlayer()->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_DEVELOPER);
            handler->GetSession()->SendNotification(LANG_DEV_ON);
            return true;
        }

        if (argstr == "off")
        {
            handler->GetSession()->GetPlayer()->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_DEVELOPER);
            handler->GetSession()->SendNotification(LANG_DEV_OFF);
            return true;
        }

        handler->SendSysMessage(LANG_USE_BOL);
        handler->SetSentErrorMessage(true);
        return false;
    }

    static bool HandleAddRPItemCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        uint32 itemId = 0;

        if (args[0] == '[')                                        // [name] manual form
        {
            char* citemName = strtok((char*)args, "]");

            if (citemName && citemName[0])
            {
                std::string itemName = citemName+1;
                WorldDatabase.EscapeString(itemName);
                QueryResult result = WorldDatabase.PQuery("SELECT entry FROM item_template WHERE entry>200000 AND name = '%s'", itemName.c_str());
                if (!result)
                {
                    handler->PSendSysMessage(LANG_COMMAND_COULDNOTFIND, citemName+1);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                itemId = result->Fetch()->GetUInt16();
            }
            else
                return false;
        }
        else                                                    // item_id or [name] Shift-click form |color|Hitem:item_id:0:0:0|h[name]|h|r
        {
            char* cId = handler->extractKeyFromLink((char*)args,"Hitem");
            if (!cId)
                return false;
            itemId = atol(cId);
        }

        if (itemId <200000)
        {
            handler->PSendSysMessage(LANG_COMMAND_RPITEMTOOLOW, itemId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        char* ccount = strtok(NULL, " ");

        int32 count = 1;

        if (ccount)
            count = strtol(ccount, NULL, 10);

        if (count == 0)
            count = 1;

        Player* pl = handler->GetSession()->GetPlayer();

        ItemPrototype const *pProto = sObjectMgr->GetItemPrototype(itemId);
        if (!pProto)
        {
            handler->PSendSysMessage(LANG_COMMAND_RPITEMIDINVALID, itemId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        //Subtract
        if (count < 0)
        {
            pl->DestroyItemCount(itemId, -count, true, false);
            handler->PSendSysMessage(LANG_REMOVERPITEM, itemId, -count, handler->GetNameLink(pl).c_str());
            return true;
        }

        //Adding items
        uint32 noSpaceForCount = 0;

        // check space and find places
        ItemPosCountVec dest;
        uint8 msg = pl->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count, &noSpaceForCount);
        if (msg != EQUIP_ERR_OK)                               // convert to possible store amount
            count -= noSpaceForCount;

        if (count == 0 || dest.empty())                         // can't add any
        {
            handler->PSendSysMessage(LANG_ITEM_CANNOT_CREATE, itemId, noSpaceForCount);
            handler->SetSentErrorMessage(true);
            return false;
        }

        Item* item = pl->StoreNewItem(dest, itemId, true, Item::GenerateItemRandomPropertyId(itemId));

        if (count > 0 && item)
        {
            pl->SendNewItem(item,count,false,true);
        }

        if (noSpaceForCount > 0)
            handler->PSendSysMessage(LANG_ITEM_CANNOT_CREATE, itemId, noSpaceForCount);

        return true;
    }

    static bool HandleSelfScaleCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        float Scale = (float)atof((char*)args);
        if (Scale > 1.15f || Scale < 0.85f)
        {
            handler->SendSysMessage(LANG_BAD_VALUE);
            handler->SetSentErrorMessage(true);
            return false;
        }

        uint8 chrRace = handler->GetSession()->GetPlayer()->getRace();
        Player *chr = handler->GetSession()->GetPlayer();
        if ((chrRace == RACE_TAUREN && Scale > 1.1f) || (chrRace == RACE_GNOME && Scale < 0.9f))
        {
            handler->SendSysMessage(LANG_BAD_SCALE_VALUE_RACE);
            handler->SetSentErrorMessage(true);
        }

        uint8 chrLevel = handler->GetSession()->GetPlayer()->getLevel();

        QueryResult result = CharacterDatabase.PQuery("SELECT scale, scale_times_changed, scale_unlocked FROM characters_addon WHERE guid='%u'", chr->GetGUIDLow());
        if(result)
        {
            Field* fields = result->Fetch();

            float customScale = fields[0].GetFloat();
            uint8 scaleTimesChanged = fields[1].GetUInt8();
            uint8 scaleUnlocked = fields[2].GetUInt8();

            if (scaleTimesChanged < 10)
            {
                //if ((scaleUnlocked == 0 && Scale > 1.1f) || (scaleUnlocked == 0 && Scale < 0.9f))
                if ((chrLevel < 85 && Scale > 1.1f) || (chrLevel < 85 && Scale < 0.9f))
                {
                    handler->SendSysMessage(LANG_BAD_SCALE_VALUE_LOCKED);
                    handler->SetSentErrorMessage(true);
                    return false;
                }

                else
                {
                    Player *chr = handler->GetSession()->GetPlayer();
		            uint8 scaleChangesRemaining = (10 - (scaleTimesChanged + 1));
                    handler->PSendSysMessage(LANG_CUSTOM_SCALE_CHANGE, customScale, Scale, scaleChangesRemaining);
                    chr->SetFloatValue(OBJECT_FIELD_SCALE_X, Scale);

                    QueryResult result = CharacterDatabase.PQuery("UPDATE characters_addon SET scale='%f', scale_times_changed=(`scale_times_changed`+1) WHERE guid='%u'", Scale, chr->GetGUIDLow());
                    return true;
                }
	        }

            else
            {
                handler->SendSysMessage(LANG_SCALE_NO_MORE_CHANGES);
                handler->SetSentErrorMessage(true);
                return false;
            }
	    }

        else
	    {
            //if ((Scale > 1.1f) || (Scale < 0.9f))
            if ((chrLevel < 85 && Scale > 1.1f) || (chrLevel < 85 && Scale < 0.9f))
                {
                    handler->SendSysMessage(LANG_BAD_SCALE_VALUE_LOCKED);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            else
            {
                Player *chr = handler->GetSession()->GetPlayer();

                uint8 scaleChangesRemaining = 9;
                handler->PSendSysMessage(LANG_CUSTOM_SCALE_CREATE, Scale, scaleChangesRemaining);
                chr->SetFloatValue(OBJECT_FIELD_SCALE_X, Scale);
                CharacterDatabase.PExecute("INSERT INTO characters_addon(guid,scale,scale_times_changed) VALUES ('%u','%f','1')", chr->GetGUIDLow(), Scale);

                return true;
            }
	    }
    }

    static bool HandleSelfTaxiCheatCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* chr = handler->GetSession()->GetPlayer();

        if (!chr->isTaxiCheater())
        {
            chr->SetTaxiCheater(true);
            handler->PSendSysMessage(LANG_SELFTAXIS_UNL);

            return true;
        }
        else
        {
            handler->SendSysMessage(LANG_SELFTAXIS_ALREADYON);
            handler->SetSentErrorMessage(true);
            return false;
        }
    }

    static bool HandleSelfMorphCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        uint16 display_id = (uint16)atoi((char*)args);

	    Player *chr = handler->GetSession()->GetPlayer();

        chr->SetDisplayId(display_id);
        handler->PSendSysMessage(LANG_SELF_MORPH, display_id);

        return true;
    }

    static bool HandleAddItemAllCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        uint32 itemId = 0;

        if (args[0] == '[')                                        // [name] manual form
        {
            char* citemName = strtok((char*)args, "]");

            if (citemName && citemName[0])
            {
                std::string itemName = citemName+1;
                WorldDatabase.EscapeString(itemName);
                QueryResult result = WorldDatabase.PQuery("SELECT entry FROM item_template WHERE name = '%s'", itemName.c_str());
                if (!result)
                {
                    handler->PSendSysMessage(LANG_COMMAND_COULDNOTFIND, citemName+1);
                    handler->SetSentErrorMessage(true);
                    return false;
                }
                itemId = result->Fetch()->GetUInt16();
            }
            else
                return false;
        }
        else                                                    // item_id or [name] Shift-click form |color|Hitem:item_id:0:0:0|h[name]|h|r
        {
            char* cId = handler->extractKeyFromLink((char*)args, "Hitem");
            if (!cId)
                return false;
            itemId = atol(cId);
        }

        char* ccount = strtok(NULL, " ");

        int32 count = 1;

        if (ccount)
            count = strtol(ccount, NULL, 10);

        if (count == 0)
            count = 1;

        ItemPrototype const* pProto = sObjectMgr->GetItemPrototype(itemId);
        if (!pProto)
        {
            handler->PSendSysMessage(LANG_COMMAND_ITEMIDINVALID, itemId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        sWorld->AddItemAll(itemId, count);

        //Subtract
        if (count < 0)
        {
            handler->PSendSysMessage(LANG_REMOVEITEM_ALL, itemId, -count);
            return true;
        }
        else
        {
            handler->PSendSysMessage(LANG_ADDITEM_ALL, itemId, count);
            return true;
        }
    
        return true;
    }

    static bool HandlePlayLocalCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        uint32 soundId = atoi((char*)args);

        if (!sSoundEntriesStore.LookupEntry(soundId))
        {
            handler->PSendSysMessage(LANG_SOUND_NOT_EXIST, soundId);
            handler->SetSentErrorMessage(true);
            return false;
        }

        WorldPacket data(SMSG_PLAY_SOUND, 4);
        data << uint32(soundId);
        handler->GetSession()->GetPlayer()->Player::SendMessageToSetInRange(&data, MAX_VISIBILITY_DISTANCE, true);

        handler->PSendSysMessage(LANG_COMMAND_PLAYED_LOCALLY, soundId);
        return true;
    }

    static bool HandleUnAuraAllCommand(ChatHandler* handler, char const* args)
    {
        std::string argstr = args;
        if (argstr == "all")
        {
            sWorld->MassUnauraAll();
            return true;
        }

        // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r or Htalent form
        uint32 spellId = handler->extractSpellIdFromLink((char*)args);
        if (!spellId)
            return false;

        sWorld->MassUnaura(spellId);

        return true;
    }

    static bool HandleMassSummonCommand(ChatHandler* handler, char const* args)
    {
        Player* target = handler->GetSession()->GetPlayer();
        uint64 guid = target->GetGUID();
        float x, y, z;
        target->GetPosition(x, y, z);
        uint32 mapId = target->GetMapId();
        uint32 zone = target->GetZoneId();
        float orient = target->GetOrientation();
        uint32 phase = target->GetPhaseMask();

        //handler->GetSession()->GetPlayer()->GetClosePoint(x, y, z, target->GetObjectSize());
        //target->TeleportTo(handler->GetSession()->GetPlayer()->GetMapId(), x, y, z, target->GetOrientation());
        //target->SetPhaseMask(handler->GetSession()->GetPlayer()->GetPhaseMask(), true);

        sWorld->MassSummon(guid, mapId, x, y, z, zone, orient, phase);

        return true;
    }

};

void AddSC_misc_commandscript()
{
    new misc_commandscript();
}
