/*
* Copyright (C) 2011-2012 ProjectStudioMirage <http://www.studio-mirage.fr/>
* Copyright (C) 2011-2012 https://github.com/Asardial
*/


#include "ScriptPCH.h"

enum TransmogrifyActions {
    ACTION_TRANSMOGRIFY_ADD_DISPLAY,
    ACTION_TRANSMOGRIFY_REMOVE_DISPLAY
};

//const uint64 PriceInGold = 1000 * 100 * 100; // 1k golds
const uint64 PriceInGold = 0;

class npc_transmogrify : public CreatureScript
{
    public:
        npc_transmogrify() : CreatureScript("npc_transmogrify") { }

        bool OnGossipHello(Player* pPlayer, Creature* pCreature)
        {
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_MONEY_BAG, "Transmogrify!", GOSSIP_SENDER_MAIN, ACTION_TRANSMOGRIFY_ADD_DISPLAY);
            pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_MONEY_BAG, "Remove Transmogrification", GOSSIP_SENDER_MAIN, ACTION_TRANSMOGRIFY_REMOVE_DISPLAY);
            pPlayer->SEND_GOSSIP_MENU(51000, pCreature->GetGUID());
            return true;
        }

        bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action)
        {
            player->PlayerTalkClass->ClearMenus();
            switch (action)
            {
                case ACTION_TRANSMOGRIFY_ADD_DISPLAY:
                    TransmogrifyItem(player, creature);
                    player->PlayerTalkClass->CloseGossip();
                    break;
                case ACTION_TRANSMOGRIFY_REMOVE_DISPLAY:
                    ClearItem(player, creature);
                    player->PlayerTalkClass->CloseGossip();
                    break;
            }
            return true;
        }

        void TransmogrifyItem(Player* player, Creature* creature)
        {
            ChatHandler handler(player);
            Item *trItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, INVENTORY_SLOT_ITEM_START);
            Item *displayItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, INVENTORY_SLOT_ITEM_START + 1);
            if (!trItem || !displayItem)
            {
                handler.PSendSysMessage(LANG_FAKEITEM_MISSING_ITEMS);
                return;
            }

            if (!player->HasEnoughMoney(PriceInGold))
            {
                handler.PSendSysMessage(LANG_FAKEITEM_MISSING_MONEY, PriceInGold);
                return;
            }

            uint8 result = trItem->SetFakeDisplay(displayItem->GetProto()->ItemId);
            switch (result)
            {
                case FAKE_ERR_CANT_FIND_ITEM:
                    handler.PSendSysMessage(LANG_FAKEITEM_MISSING_ITEM);
                    break;
/*
//                case FAKE_ERR_WRONG_QUALITY:
//                    handler.PSendSysMessage("Votre item n'est pas de bonne qualité!");
//                    break;
*/
                case FAKE_ERR_DIFF_INVENTORYTYPE:
                    handler.PSendSysMessage(LANG_FAKEITEM_DIFF_TYPE);
                    break;
/*
//                case FAKE_ERR_DIFF_CLASS:
//                    handler.PSendSysMessage("Votre item n'est pas pour votre Classe!");
//                    break;
//
//                case FAKE_ERR_DIFF_RACE:
//                    handler.PSendSysMessage("Votre item n'est pas pour votre Race!");
//                    break;
*/
                case FAKE_ERR_DIFF_SUBCLASS:
                    handler.PSendSysMessage(LANG_FAKEITEM_DIFF_TYPE);
                    break;

                case FAKE_ERR_INVALID_CLASS:
                    handler.PSendSysMessage(LANG_FAKEITEM_INVALID_TYPE);
                    break;

                case FAKE_ERR_OK:
                {
                    WorldPacket data;
                    data << uint8(INVENTORY_SLOT_BAG_0);
                    data << uint8(trItem->GetSlot());
                    player->GetSession()->HandleAutoEquipItemOpcode(data);

                    player->ModifyMoney(-1 * PriceInGold);
                    creature->GetAI()->DoCast(63491);

                    break;
                }
            }
        }

        void ClearItem(Player *player, Creature* creature)
        {
            ChatHandler handler(player);
            Item *trItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, INVENTORY_SLOT_ITEM_START);
            if (!trItem)
            {
                handler.PSendSysMessage(LANG_FAKEITEM_CLEARITEM);
                return;
            }

            trItem->RemoveFakeDisplay();

            WorldPacket data;
            data << uint8(INVENTORY_SLOT_BAG_0);
            data << uint8(trItem->GetSlot());
            player->GetSession()->HandleAutoEquipItemOpcode(data);

            creature->GetAI()->DoCast(63491);
        }
};

void AddSC_transmogrify_script()
{
    new npc_transmogrify;
}