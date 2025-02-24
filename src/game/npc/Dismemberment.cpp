/*
 * Copyright 2014-2021 Arx Libertatis Team (see the AUTHORS file)
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

#include "game/npc/Dismemberment.h"

#include <string_view>
#include <boost/algorithm/string.hpp>

#include "core/GameTime.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Item.h"
#include "game/NPC.h"
#include "graphics/GraphicsTypes.h"
#include "graphics/data/MeshManipulation.h"

#include "physics/CollisionShapes.h"
#include "physics/Physics.h"

#include "scene/GameSound.h"

static bool IsNearSelection(EERIE_3DOBJ * obj, long vert, ObjSelection tw) {
	
	if(!obj || tw == ObjSelection() || vert < 0) {
		return false;
	}
	
	for(size_t vertex : obj->selections[tw.handleData()].selected) {
		float d = glm::distance(obj->vertexlist[vertex].v, obj->vertexlist[vert].v);
		if(d < 8.f) {
			return true;
		}
	}
	
	return false;
}

/*!
 * \brief Spawns a body part from NPC
 */
static void ARX_NPC_SpawnMember(Entity * ioo, ObjSelection num) {
	
	if(!ioo) {
		return;
	}
	
	EERIE_3DOBJ * from = ioo->obj;
	if(!from || num == ObjSelection() || size_t(num.handleData()) >= from->selections.size()) {
		return;
	}
	
	EERIE_3DOBJ * nouvo = new EERIE_3DOBJ;
	if(!nouvo) {
		return;
	}
	
	long gore = -1;
	for(size_t k = 0; k < from->texturecontainer.size(); k++) {
		if(from->texturecontainer[k] && boost::contains(from->texturecontainer[k]->m_texName.string(), "gore")) {
			gore = k;
			break;
		}
	}
	
	size_t nvertex = from->selections[num.handleData()].selected.size();
	for(EERIE_FACE & face : from->facelist) {
		if(face.texid == gore
		   && (IsNearSelection(from, face.vid[0], num)
		       || IsNearSelection(from, face.vid[1], num)
		       || IsNearSelection(from, face.vid[2], num))) {
			nvertex += 3;
		}
	}
	
	nouvo->vertexlist.resize(nvertex);
	nouvo->vertexWorldPositions.resize(nvertex);
	nouvo->vertexClipPositions.resize(nvertex);
	nouvo->vertexColors.resize(nvertex);
	
	size_t inpos = 0;
	
	std::vector<long> equival(from->vertexlist.size(), -1);
	
	const EERIE_SELECTIONS & cutSelection = from->selections[num.handleData()];
	
	arx_assert(0 < cutSelection.selected.size());
	
	for(size_t k = 0; k < cutSelection.selected.size(); k++) {
		inpos = cutSelection.selected[k];
		equival[cutSelection.selected[k]] = k;
		nouvo->vertexlist[k] = from->vertexlist[cutSelection.selected[k]];
		nouvo->vertexlist[k].v = from->vertexWorldPositions[cutSelection.selected[k]].v;
		nouvo->vertexlist[k].v -= ioo->pos;
		nouvo->vertexWorldPositions[k] = nouvo->vertexlist[k];
	}
	
	size_t count = cutSelection.selected.size();
	for(const EERIE_FACE & face : from->facelist) {
		if(face.texid == gore) {
			if(IsNearSelection(from, face.vid[0], num)
			   || IsNearSelection(from, face.vid[1], num)
			   || IsNearSelection(from, face.vid[2], num)) {
				
				for(long j = 0; j < 3; j++) {
					if(count < nouvo->vertexlist.size()) {
						nouvo->vertexlist[count] = from->vertexlist[face.vid[j]];
						nouvo->vertexlist[count].v = from->vertexWorldPositions[face.vid[j]].v;
						nouvo->vertexlist[count].v -= ioo->pos;
						nouvo->vertexWorldPositions[count] = nouvo->vertexlist[count];
						equival[face.vid[j]] = count;
					} else {
						equival[face.vid[j]] = -1;
					}
					count++;
				}
				
			}
		}
	}
	
	float min = nouvo->vertexlist[0].v.y;
	long nummm = 0;
	for(size_t k = 1; k < nouvo->vertexlist.size(); k++) {
		if(nouvo->vertexlist[k].v.y > min) {
			min = nouvo->vertexlist[k].v.y;
			nummm = k;
		}
	}
	
	nouvo->origin = nummm;
	
	Vec3f point0 = nouvo->vertexlist[nouvo->origin].v;
	for(EERIE_VERTEX & vertex : nouvo->vertexlist) {
		vertex.v -= point0;
	}
	
	nouvo->pbox = nullptr;
	
	size_t nfaces = 0;
	for(const EERIE_FACE & face : from->facelist) {
		if(equival[face.vid[0]] != -1 && equival[face.vid[1]] != -1 && equival[face.vid[2]] != -1) {
			nfaces++;
		}
	}
	
	if(nfaces) {
		
		nouvo->facelist.reserve(nfaces);
		for(const EERIE_FACE & face : from->facelist) {
			if(equival[face.vid[0]] != -1 && equival[face.vid[1]] != -1 && equival[face.vid[2]] != -1) {
				EERIE_FACE newface = face;
				newface.vid[0] = static_cast<unsigned short>(equival[face.vid[0]]);
				newface.vid[1] = static_cast<unsigned short>(equival[face.vid[1]]);
				newface.vid[2] = static_cast<unsigned short>(equival[face.vid[2]]);
				nouvo->facelist.push_back(newface);
			}
		}
		
		long goreTexture = -1;
		for(size_t k = 0; k < from->texturecontainer.size(); k++) {
			if(from->texturecontainer[k]
			   && boost::contains(from->texturecontainer[k]->m_texName.string(), "gore")
			) {
				goreTexture = k;
				break;
			}
		}
		
		for(EERIE_FACE & face : nouvo->facelist) {
			face.facetype &= ~POLY_HIDE;
			if(face.texid == goreTexture) {
				face.facetype |= POLY_DOUBLESIDED;
			}
		}
		
	}
	
	nouvo->texturecontainer = from->texturecontainer;
	
	nouvo->linked.clear();
	nouvo->originaltextures.clear();
	
	Entity * io = new Entity("noname", EntityInstance(0));
	
	io->_itemdata = new IO_ITEMDATA();
	
	io->ioflags = IO_ITEM | IO_NOSAVE | IO_MOVABLE;
	io->script.valid = false;
	io->script.data.clear();
	io->gameFlags |= GFLAG_NO_PHYS_IO_COL;
	
	EERIE_COLLISION_Cylinder_Create(io);
	EERIE_PHYSICS_BOX_Create(nouvo);
	if(!nouvo->pbox){
		delete nouvo;
		return;
	}
	
	io->infracolor = Color3f::blue * 0.8f;
	io->collision = COLLIDE_WITH_PLAYER;
	io->m_icon = nullptr;
	io->scriptload = 1;
	io->obj = nouvo;
	io->lastpos = io->initpos = io->pos = ioo->obj->vertexWorldPositions[inpos].v;
	io->angle = ioo->angle;
	
	io->gameFlags = ioo->gameFlags;
	io->halo = ioo->halo;
	
	io->angle.setPitch(Random::getf(340.f, 380.f));
	io->angle.setYaw(Random::getf(0.f, 360.f));
	io->angle.setRoll(0);
	io->obj->pbox->active = 1;
	io->obj->pbox->stopcount = 0;
	
	Vec3f vector(-std::sin(glm::radians(io->angle.getYaw())),
	             std::sin(glm::radians(io->angle.getPitch())) * 2.f,
	             std::cos(glm::radians(io->angle.getYaw())));
	vector = glm::normalize(vector);
	io->rubber = 0.6f;
	
	io->no_collide = ioo->index();
	
	io->gameFlags |= GFLAG_GOREEXPLODE;
	io->animBlend.lastanimtime = g_gameTime.now();
	io->soundtime = 0;
	io->soundcount = 0;

	EERIE_PHYSICS_BOX_Launch(io->obj, io->pos, io->angle, vector);
}



static DismembermentFlag GetCutFlag(std::string_view str) {
	
	if(str == "cut_head") {
		return FLAG_CUT_HEAD;
	}
	if(str == "cut_torso") {
		return FLAG_CUT_TORSO;
	}
	if(str == "cut_larm") {
		return FLAG_CUT_LARM;
	}
	if(str == "cut_rarm") {
		return FLAG_CUT_HEAD;
	}
	if(str == "cut_lleg") {
		return FLAG_CUT_LLEG;
	}
	if(str == "cut_rleg") {
		return FLAG_CUT_RLEG;
	}
	
	return DismembermentFlag(0);
}

static ObjSelection GetCutSelection(Entity * io, DismembermentFlag flag) {
	
	if(!io || !(io->ioflags & IO_NPC) || flag == 0)
		return ObjSelection();

	std::string_view tx;
	if(flag == FLAG_CUT_HEAD) {
		tx =  "cut_head";
	} else if(flag == FLAG_CUT_TORSO) {
		tx = "cut_torso";
	} else if(flag == FLAG_CUT_LARM) {
		tx = "cut_larm";
	} else if(flag == FLAG_CUT_RARM) {
		tx = "cut_rarm";
	} else if(flag == FLAG_CUT_LLEG) {
		tx = "cut_lleg";
	} else if(flag == FLAG_CUT_RLEG) {
		tx = "cut_rleg";
	}
	
	if(!tx.empty()) {
		typedef std::vector<EERIE_SELECTIONS>::iterator iterator; // Convenience
		for(iterator iter = io->obj->selections.begin(); iter != io->obj->selections.end(); ++iter) {
			if(!iter->selected.empty() && iter->name == tx) {
				return ObjSelection(iter - io->obj->selections.begin());
			}
		}
	}

	return ObjSelection();
}

static void ReComputeCutFlags(Entity * io) {
	
	if(!io || !(io->ioflags & IO_NPC))
		return;

	if(io->_npcdata->cuts & FLAG_CUT_TORSO) {
		io->_npcdata->cuts &= ~FLAG_CUT_HEAD;
		io->_npcdata->cuts &= ~FLAG_CUT_LARM;
		io->_npcdata->cuts &= ~FLAG_CUT_RARM;
	}
}

static bool IsAlreadyCut(Entity * io, DismembermentFlag fl) {
	
	if(io->_npcdata->cuts & fl)
		return true;

	if(io->_npcdata->cuts & FLAG_CUT_TORSO) {
		if(fl == FLAG_CUT_HEAD)
			return true;

		if(fl == FLAG_CUT_LARM)
			return true;

		if(fl == FLAG_CUT_RARM)
			return true;
	}

	return false;
}

static bool applyCuts(Entity & npc) {
	
	arx_assert(npc.ioflags & IO_NPC);
	
	if(npc._npcdata->cuts == 0) {
		return false; // No cuts
	}
	
	ReComputeCutFlags(&npc);
	
	long goretex = -1;
	for(size_t i = 0; i < npc.obj->texturecontainer.size(); i++) {
		if(npc.obj->texturecontainer[i]
		   && boost::contains(npc.obj->texturecontainer[i]->m_texName.string(), "gore")) {
			goretex = i;
			break;
		}
	}
	
	for(EERIE_FACE & face : npc.obj->facelist) {
		face.facetype &= ~POLY_HIDE;
	}
	
	bool hid = false;
	for(long jj = 0; jj < 6; jj++) {
		DismembermentFlag flg = DismembermentFlag(1 << jj);
		
		ObjSelection numsel = GetCutSelection(&npc, flg);
		if(!(npc._npcdata->cuts & flg) || numsel == ObjSelection()) {
			continue;
		}
		
		for(EERIE_FACE & face : npc.obj->facelist) {
			if(IsInSelection(npc.obj, face.vid[0], numsel)
			   || IsInSelection(npc.obj, face.vid[1], numsel)
			   || IsInSelection(npc.obj, face.vid[2], numsel)) {
				if(!(face.facetype & POLY_HIDE) && face.texid != goretex) {
					hid = true;
				}
				face.facetype |= POLY_HIDE;
			}
		}
		
		npc._npcdata->cut = 1;
		
	}
	
	return hid;
}

void ARX_NPC_TryToCutSomething(Entity * target, const Vec3f * pos) {
	
	if(!target || !(target->ioflags & IO_NPC))
		return;

	if(target->gameFlags & GFLAG_NOGORE)
		return;

	float mindistSqr = std::numeric_limits<float>::max();
	ObjSelection numsel = ObjSelection();
	long goretex = -1;

	for(size_t i = 0; i < target->obj->texturecontainer.size(); i++) {
		if(target->obj->texturecontainer[i]
		   && boost::contains(target->obj->texturecontainer[i]->m_texName.string(), "gore")
		) {
			goretex = i;
			break;
		}
	}

	for(size_t i = 0; i < target->obj->selections.size(); i++) {
		ObjSelection sel = ObjSelection(i);
		
		if(!target->obj->selections[i].selected.empty()
		   && boost::contains(target->obj->selections[i].name, "cut_")) {
			
			DismembermentFlag fll = GetCutFlag(target->obj->selections[i].name);

			if(IsAlreadyCut(target, fll))
				continue;

			long out = 0;

			for(size_t ll = 0; ll < target->obj->facelist.size(); ll++) {
				EERIE_FACE & face = target->obj->facelist[ll];

				if(face.texid != goretex) {
					if(   IsInSelection(target->obj, face.vid[0], sel)
					   || IsInSelection(target->obj, face.vid[1], sel)
					   || IsInSelection(target->obj, face.vid[2], sel)
					) {
						if(face.facetype & POLY_HIDE) {
							out++;
						}
					}
				}
			}

			if(out < 3) {
				float dist = arx::distance2(*pos, target->obj->vertexWorldPositions[target->obj->selections[i].selected[0]].v);

				if(dist < mindistSqr) {
					mindistSqr = dist;
					numsel = sel;
				}
			}
		}
	}

	if(numsel == ObjSelection())
		return; // Nothing to cut...

	bool hid = false;
	if(mindistSqr < square(60)) { // can only cut a close part...
		DismembermentFlag fl = GetCutFlag(target->obj->selections[numsel.handleData()].name);
		if(fl && !(target->_npcdata->cuts & fl)) {
			target->_npcdata->cuts |= fl;
			hid = applyCuts(*target);
		}
	}
	
	if(hid) {
		ARX_SOUND_PlaySFX(g_snd.DISMEMBER, &target->pos, 1.0f);
		ARX_NPC_SpawnMember(target, numsel);
	}
	
}


void ARX_NPC_RestoreCuts() {
	
	for(Entity & npc : entities(IO_NPC)) {
		if(npc._npcdata->cuts) {
			applyCuts(npc);
		}
	}
	
}
