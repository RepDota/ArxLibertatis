/*
 * Copyright 2011-2021 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */
/* Based on:
===========================================================================
ARX FATALIS GPL Source Code
Copyright (C) 1999-2010 Arkane Studios SA, a ZeniMax Media company.

This file is part of the Arx Fatalis GPL Source Code ('Arx Fatalis Source Code'). 

Arx Fatalis Source Code is free software: you can redistribute it and/or modify it under the terms of the GNU General Public 
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Arx Fatalis Source Code is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied 
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Arx Fatalis Source Code.  If not, see 
<http://www.gnu.org/licenses/>.

In addition, the Arx Fatalis Source Code is also subject to certain additional terms. You should have received a copy of these 
additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Arx 
Fatalis Source Code. If not, please request a copy in writing from Arkane Studios at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing Arkane Studios, c/o 
ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/

#include "script/ScriptedInventory.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "game/NPC.h"
#include "graphics/Math.h"
#include "graphics/data/Mesh.h"
#include "gui/hud/SecondaryInventory.h"
#include "io/resource/ResourcePath.h"
#include "scene/Interactive.h"
#include "scene/GameSound.h"
#include "script/ScriptUtils.h"

extern Entity * LASTSPAWNED;

namespace script {

namespace {

class InventoryCommand : public Command {
	
	class SubCommand : public Command {
		
		const std::string command;
		
	public:
		
		explicit SubCommand(std::string_view name)
			: Command(std::string("inventory ") += name, AnyEntity), command(name) { }
		
		const std::string & getCommand() { return command; }
		
	};
	
	std::map<std::string_view, std::unique_ptr<SubCommand>> m_commands;
	
	void addCommand(SubCommand * command) {
		[[maybe_unused]] auto res = m_commands.emplace(command->getCommand(), command);
		arx_assert_msg(res.second, "Duplicate script inventory command name: %s", command->getCommand().c_str());
	}
	
	class DestroyCommand : public SubCommand {
		
	public:
		
		static void destroyInventory(Entity * io) {
			
			if(!io->inventory) {
				return;
			}
			
			g_secondaryInventoryHud.clear(io);
			
			for(auto slot : io->inventory->slots()) {
				Entity * item = slot.entity;
				if(item) {
					removeFromInventories(item);
					// Delay destruction of the object to avoid invalid references
					if(item->ioflags & IO_ITEM) {
						item->_itemdata->count = 1;
					}
					ARX_INTERACTIVE_DestroyIOdelayed(item);
					// Prevent further script events as the object has been destroyed!
					item->show = SHOW_FLAG_MEGAHIDE;
					item->ioflags |= IO_FREEZESCRIPT;
				}
				arx_assert(slot.entity == nullptr);
			}
			
			io->inventory.reset();
		}
		
		DestroyCommand() : SubCommand("destroy") { }
		
		Result execute(Context & context) override {
			
			DebugScript("");
			
			destroyInventory(context.getEntity());
			
			return Success;
		}
		
	};
	
	class CreateCommand : public SubCommand {
		
	public:
		
		CreateCommand() : SubCommand("create") { }
		
		Result execute(Context & context) override {
			
			DebugScript("");
			
			Entity * io = context.getEntity();
			
			DestroyCommand::destroyInventory(io);
			
			io->inventory = std::make_unique<Inventory>(io, Vec2s(3, 11));
			
			return Success;
		}
		
	};
	
	class SkinCommand : public SubCommand {
		
	public:
		
		SkinCommand() : SubCommand("skin") { }
		
		Result execute(Context & context) override {
			
			context.getEntity()->inventory_skin = res::path::load(context.getWord());
			
			DebugScript(' ' << context.getEntity()->inventory_skin);
			
			return Success;
		}
		
	};
	
	class PlayerAddFromSceneCommand : public SubCommand {
		
	public:
		
		PlayerAddFromSceneCommand() : SubCommand("playeraddfromscene") { }
		
		Result execute(Context & context) override {
			
			std::string target = context.getWord();
			
			DebugScript(' ' << target);
			
			Entity * t = entities.getById(target, context.getEntity());
			if(!t) {
				ScriptWarning << "unknown target: " << target;
				return Failed;
			}
			
			giveToPlayer(t);
			
			return Success;
		}
		
	};
	
	class PlayerAddCommand : public SubCommand {
		
		const bool multi;
		
	public:
		
		PlayerAddCommand(std::string_view name, bool _multi) : SubCommand(name), multi(_multi) { }
		
		Result execute(Context & context) override {
			
			res::path file = res::path::load(context.getWord());
			
			if(FORBID_SCRIPT_IO_CREATION) {
				if(multi) {
					context.skipWord();
				}
				return Failed;
			}
			
			file = "graph/obj3d/interactive/items" / file;
			
			Entity * ioo = AddItem(file);
			if(!ioo) {
				ScriptWarning << "could not add item " << file;
				return Failed;
			}
			
			LASTSPAWNED = ioo;
			ioo->scriptload = 1;
			SendInitScriptEvent(ioo);
			
			if(multi) {
				
				float count = context.getFloat();
				
				DebugScript(' ' << file << ' ' << count);
				
				if(ioo->ioflags & IO_GOLD) {
					ioo->_itemdata->price = static_cast<long>(count);
				} else {
					ioo->_itemdata->maxcount = 9999;
					ioo->_itemdata->count = std::max(checked_range_cast<short>(count), short(1));
				}
				
			} else {
				DebugScript(' ' << file);
			}
			
			giveToPlayer(ioo);
			
			return Success;
		}
		
	};
	
	class AddFromSceneCommand : public SubCommand {
		
	public:
		
		AddFromSceneCommand() : SubCommand("addfromscene") { }
		
		Result execute(Context & context) override {
			
			std::string target = context.getWord();
			
			DebugScript(' ' << target);
			
			Entity * item = entities.getById(target, context.getEntity());
			if(!item) {
				ScriptWarning << "unknown target: " << target;
				return Failed;
			}
			
			if(isEquippedByPlayer(item)) {
				ARX_EQUIPMENT_UnEquip(entities.player(), item, 1);
			}
			
			item->scriptload = 0;
			
			if(!context.getEntity()->inventory || !context.getEntity()->inventory->insert(item)) {
				PutInFrontOfPlayer(item);
			}
			
			return Success;
		}
		
	};
	
	class AddCommand : public SubCommand {
		
		const bool multi;
		
	public:
		
		AddCommand(std::string_view name, bool _multi) : SubCommand(name), multi(_multi) { }
		
		Result execute(Context & context) override {
			
			res::path file = res::path::load(context.getWord());
			
			Entity * io = context.getEntity();
			
			if(FORBID_SCRIPT_IO_CREATION || !io->inventory) {
				if(multi) {
					context.skipWord();
				}
				return Failed;
			}
			
			file = "graph/obj3d/interactive/items" / file;
			
			long count = -1;
			if(multi) {
				float val = context.getFloat();
				
				DebugScript(' ' << file << ' ' << val);
				
				count = static_cast<long>(val);
				
			} else {
				DebugScript(' ' << file);
			}
			
			Entity * item = AddItem(file);
			if(!item) {
				ScriptWarning << "could not add item " << file;
				return Failed;
			}
			
			if(!count) {
				return Success;
			}
			
			LASTSPAWNED = item;
			item->scriptload = 1;
			SendInitScriptEvent(item);
			
			if(multi) {
				if(item->ioflags & IO_GOLD) {
					item->_itemdata->price = count;
				} else {
					item->_itemdata->maxcount = 9999;
					item->_itemdata->count = std::max(checked_range_cast<short>(count), short(1));
				}
			}
			
			if(!context.getEntity()->inventory || !context.getEntity()->inventory->insert(item)) {
				PutInFrontOfPlayer(item);
			}
			
			return Success;
		}
		
	};
	
	class OpenCommand : public SubCommand {
		
	public:
		
		OpenCommand() : SubCommand("open") { }
		
		Result execute(Context & context) override {
			
			DebugScript("");
			
			if(context.getEntity()->inventory && !g_secondaryInventoryHud.isOpen(context.getEntity())) {
				g_secondaryInventoryHud.open(context.getEntity());
				ARX_SOUND_PlayInterface(g_snd.BACKPACK);
			}
			
			return Success;
		}
		
	};
	
	class CloseCommand : public SubCommand {
		
	public:
		
		CloseCommand() : SubCommand("close") { }
		
		Result execute(Context & context) override {
			
			DebugScript("");
			
			if(context.getEntity()->inventory && g_secondaryInventoryHud.isOpen()) {
				g_secondaryInventoryHud.close();
				ARX_SOUND_PlayInterface(g_snd.BACKPACK);
			}
			
			return Success;
		}
		
	};
	
public:
	
	InventoryCommand() : Command("inventory", AnyEntity) {
		addCommand(new CreateCommand);
		addCommand(new SkinCommand);
		addCommand(new PlayerAddFromSceneCommand);
		addCommand(new PlayerAddCommand("playeradd", false));
		addCommand(new PlayerAddCommand("playeraddmulti", true));
		addCommand(new AddFromSceneCommand);
		addCommand(new AddCommand("add", false));
		addCommand(new AddCommand("addmulti", true));
		addCommand(new DestroyCommand);
		addCommand(new OpenCommand);
		addCommand(new CloseCommand);
	}
	
	Result execute(Context & context) override {
		
		std::string cmdname = context.getWord();
		
		// Remove all underscores from the command.
		cmdname.resize(std::remove(cmdname.begin(), cmdname.end(), '_') - cmdname.begin());
		
		auto it = m_commands.find(cmdname);
		if(it == m_commands.end()) {
			ScriptWarning << "unknown inventory command: " << cmdname;
			return Failed;
		}
		
		return it->second->execute(context);
	}
	
};

class EquipCommand : public Command {
	
public:
	
	EquipCommand() : Command("equip", AnyEntity) { }
	
	Result execute(Context & context) override {
		
		bool unequip = false;
		HandleFlags("r") {
			unequip = test_flag(flg, 'r');
		}
		
		std::string target = context.getWord();
		
		DebugScript(' ' << options << ' ' << target);
		
		Entity * entity = entities.getById(target);
		if(!entity) {
			ScriptWarning << "unknown target: " << target;
			return Failed;
		}
		
		if(unequip) {
			Stack_SendIOScriptEvent(entity, context.getEntity(), SM_EQUIPOUT);
			ARX_EQUIPMENT_UnEquip(entity, context.getEntity());
		} else {
			Stack_SendIOScriptEvent(entity, context.getEntity(), SM_EQUIPIN);
			ARX_EQUIPMENT_Equip(entity, context.getEntity());
		}
		
		return Success;
	}
	
};

class WeaponCommand : public Command {
	
public:
	
	WeaponCommand() : Command("weapon", IO_NPC) { }
	
	Result execute(Context & context) override {
		
		bool draw = context.getBool();
		
		DebugScript(' ' << draw);
		
		Entity * io = context.getEntity();
		
		if(draw) {
			if(io->_npcdata->weaponinhand == 0) {
				AcquireLastAnim(io);
				FinishAnim(io, io->animlayer[1].cur_anim);
				io->animlayer[1].cur_anim = nullptr;
				io->_npcdata->weaponinhand = -1;
			}
		} else {
			if(io->_npcdata->weaponinhand == 1) {
				AcquireLastAnim(io);
				FinishAnim(io, io->animlayer[1].cur_anim);
				io->animlayer[1].cur_anim = nullptr;
				io->_npcdata->weaponinhand = 2;
			}
		}
		
		return Success;
	}
	
};

class SetWeaponCommand : public Command {
	
public:
	
	SetWeaponCommand() : Command("setweapon", IO_NPC) { }
	
	Result execute(Context & context) override {
		
		Entity * io = context.getEntity();
		
		io->gameFlags &= ~GFLAG_HIDEWEAPON;
		HandleFlags("h") {
			if(flg & flag('h')) {
				io->gameFlags |= GFLAG_HIDEWEAPON;
			}
		}
		
		res::path weapon = res::path::load(context.getWord());
		
		DebugScript(' ' << options << ' ' << weapon);
		
		Prepare_SetWeapon(io, weapon);
		
		return Success;
	}
	
};

} // anonymous namespace

void setupScriptedInventory() {
	
	ScriptEvent::registerCommand(std::make_unique<InventoryCommand>());
	ScriptEvent::registerCommand(std::make_unique<EquipCommand>());
	ScriptEvent::registerCommand(std::make_unique<WeaponCommand>());
	ScriptEvent::registerCommand(std::make_unique<SetWeaponCommand>());
	
}

} // namespace script
