// NPCs for FLHookPlugin
// December 2015 by BestDiscoveryHookDevs2015
//
// 
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Includes
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <map>
#include <unordered_set>
#include <algorithm>
#include <FLHook.h>
#include <plugin.h>
#include <PluginUtilities.h>
#include "Main.h"
#include <sstream>
#include <iostream>
#include <random>

#define RIGHT_CHECK(a) if(!(cmds->rights & a)) { cmds->Print(L"ERR No permission\n"); return; }
static int set_iPluginDebug = 0;

/// A return code to indicate to FLHook if we want the hook processing to continue.
PLUGIN_RETURNCODE returncode;

void LoadSettings();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	srand((uint)time(0));
	// If we're being loaded from the command line while FLHook is running then
	// set_scCfgFile will not be empty so load the settings as FLHook only
	// calls load settings on FLHook startup and .rehash.
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		if (set_scCfgFile.length() > 0)
			LoadSettings();

		HkLoadStringDLLs();
	}
	else if (fdwReason == DLL_PROCESS_DETACH)
	{
		HkUnloadStringDLLs();
	}
	return true;
}

/// Hook will call this function after calling a plugin function to see if we the
/// processing to continue
EXPORT PLUGIN_RETURNCODE Get_PluginReturnCode()
{
	return returncode;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//STRUCTURES AND DEFINITIONS
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

vector<const char*> listgraphs;

vector<uint> npcnames;
unordered_set<uint> npcs;
map<wstring, vector<uint>> npcsGroups;

unordered_set<wstring> encountersToInit;
unordered_set<uint> npcsToWatchDamage;
unordered_set<uint> npcsToWatchEngagePlayers;

int ailoot = 0;

struct NPC_ARCHTYPESSTRUCT
{
	uint Shiparch;
	uint Loadout;
	uint IFF;
	uint Infocard;
	uint Infocard2;
	int Graph;
	string PilotNickname = "pilot_bh_ace";
};

struct NPC_FLEETSTRUCT
{
	wstring fleetname;
	map<wstring, int> fleetmember;
};

struct COORDS
{
	Vector pos;
	Matrix ori;
	float spread;
};

static map<wstring, NPC_ARCHTYPESSTRUCT> mapNPCArchtypes;
static map<wstring, NPC_FLEETSTRUCT> mapNPCFleets;
static map<wstring, COORDS> coordList;

pub::AI::SetPersonalityParams HkMakePersonality(int graphid, const std::string &pilotNickname)
{

	pub::AI::SetPersonalityParams p;
	p.state_graph = pub::StateGraph::get_state_graph(listgraphs[graphid], pub::StateGraph::TYPE_STANDARD);
	p.state_id = true;

	p.personality = Personalities::GetPersonality(pilotNickname);
	return p;
}

float rand_FloatRange(float a, float b)
{
	return ((b - a) * ((float)rand() / RAND_MAX)) + a;
}

uint rand_name()
{
	int randomIndex = rand() % npcnames.size();
	return npcnames.at(randomIndex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Loading Settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LoadNPCInfo()
{
	// The path to the configuration file.
	char szCurDir[MAX_PATH];
	GetCurrentDirectory(sizeof(szCurDir), szCurDir);
	string scPluginCfgFile = string(szCurDir) + "\\flhook_plugins\\alley_npc.cfg";

	INI_Reader ini;
	if (ini.open(scPluginCfgFile.c_str(), false))
	{
		while (ini.read_header())
		{
			if (ini.is_header("npcs"))
			{
				NPC_ARCHTYPESSTRUCT setnpcstruct;
				while (ini.read_value())
				{
					if (ini.is_value("npc"))
					{
						string setnpcname = ini.get_value_string(0);
						wstring thenpcname = stows(setnpcname);
						setnpcstruct.Shiparch = CreateID(ini.get_value_string(1));
						setnpcstruct.Loadout = CreateID(ini.get_value_string(2));

						// IFF calc
						pub::Reputation::GetReputationGroup(setnpcstruct.IFF, ini.get_value_string(3));

						// Selected graph
						setnpcstruct.Graph = ini.get_value_int(4);

						// Infocard
						setnpcstruct.Infocard = ini.get_value_int(5);
						setnpcstruct.Infocard2 = ini.get_value_int(6);

						mapNPCArchtypes[thenpcname] = setnpcstruct;
					}
				}
			}
			else if (ini.is_header("fleet"))
			{
				NPC_FLEETSTRUCT setfleet;
				wstring thefleetname;
				while (ini.read_value())
				{
					if (ini.is_value("fleetname"))
					{
						string setfleetname = ini.get_value_string(0);
						thefleetname = stows(setfleetname);
						setfleet.fleetname = stows(setfleetname);
					}
					else if (ini.is_value("fleetmember"))
					{
						string setmembername = ini.get_value_string(0);
						wstring membername = stows(setmembername);
						int amount = ini.get_value_int(1);
						setfleet.fleetmember[membername] = amount;
					}
				}
				mapNPCFleets[thefleetname] = setfleet;
			}
			else if (ini.is_header("names"))
			{
				while (ini.read_value())
				{
					if (ini.is_value("name"))
					{
						npcnames.push_back(ini.get_value_int(0));
					}
				}
			}
		}
		ini.close();
	}



}

void LoadSettings()
{
	returncode = DEFAULT_RETURNCODE;

	LoadNPCInfo();

	listgraphs.push_back("FIGHTER"); // 0
	listgraphs.push_back("TRANSPORT"); // 1
	listgraphs.push_back("GUNBOAT"); // 2
	listgraphs.push_back("CRUISER"); // 3, doesn't seem to do anything
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

FILE* Logfile = fopen("./flhook_logs/npc_log.log", "at");

void Logging(const char* szString, ...)
{
	char szBufString[1024];
	va_list marker;
	va_start(marker, szString);
	_vsnprintf(szBufString, sizeof(szBufString) - 1, szString, marker);

	char szBuf[64];
	time_t tNow = time(0);
	struct tm* t = localtime(&tNow);
	strftime(szBuf, sizeof(szBuf), "%d/%m/%Y %H:%M:%S", t);
	fprintf(Logfile, "%s %s\n", szBuf, szBufString);
	fflush(Logfile);
	fclose(Logfile);
	Logfile = fopen("./flhook_logs/npc_log.log", "at");
}

bool IsFLHookNPC(CShip* ship)
{
	// if it's a player do nothing
	if (ship->ownerPlayer)
	{
		return false;
	}

	uint shipID = ship->get_id();
	// is it an flhook npc
	const auto& foundNPC = npcs.find(shipID);
	if (foundNPC == npcs.end())
	{
		return false;
	}
	
	ship->clear_equip_and_cargo();
	npcs.erase(foundNPC);
	for (auto& npcGroup : npcsGroups)
	{
		auto& groupList = npcGroup.second;
		for (auto& i = groupList.begin(); i != groupList.end(); i++)
		{
			if (*i == shipID)
			{
				npcGroup.second.erase(i);
				return true;
			}
		}
	}
	

	return false;
}


void Log_CreateNPC(const wstring& name)
{
	//internal log
	wstring wscMsgLog = L"created <%name>";
	wscMsgLog = ReplaceStr(wscMsgLog, L"%name", name.c_str());
	string scText = wstos(wscMsgLog);
	Logging("%s", scText.c_str());
}

void __stdcall ShipDestroyed(IObjRW* iobj, bool isKill, uint killerId)
{
	returncode = DEFAULT_RETURNCODE;
	IsFLHookNPC(reinterpret_cast<CShip*>(iobj->cobj));
}

void CreateNPC(const NPC_ARCHTYPESSTRUCT& arch, Vector pos, Matrix& rot, uint iSystem, const wstring& groupName, const wstring& coordName)
{
	if (coordList.count(coordName))
	{
		auto& coords = coordList.at(coordName);
		pos.x = coords.pos.x + rand_FloatRange(-coords.spread, coords.spread);
		pos.y = coords.pos.y + rand_FloatRange(-coords.spread, coords.spread);
		pos.z = coords.pos.z + rand_FloatRange(-coords.spread, coords.spread);
		rot = coords.ori;

	}
	else
	{
		pos.x += rand_FloatRange(0, 1000);
		pos.y += rand_FloatRange(0, 1000);
		pos.z += rand_FloatRange(0, 1000);
	}

	pub::SpaceObj::ShipInfo si;
	memset(&si, 0, sizeof(si));
	si.iFlag = 1;
	si.iSystem = iSystem;
	si.iShipArchetype = arch.Shiparch;
	si.vPos = pos;
	si.mOrientation = rot;
	si.iLoadout = arch.Loadout;
	si.iLook1 = CreateID("li_captain_head");
	si.iLook2 = CreateID("li_male_guard_body");
	si.iComm = CreateID("comm_ge_generic2");
	si.iPilotVoice = CreateID("TLEADER_voice_m03");
	si.iHealth = -1;
	si.iLevel = 19;

	// Define the string used for the scanner name. Because the
	// following entry is empty, the pilot_name is used. This
	// can be overriden to display the ship type instead.
	FmtStr scanner_name(0, 0);
	scanner_name.begin_mad_lib(0);
	scanner_name.end_mad_lib();

	// Define the string used for the pilot name. The example
	// below shows the use of multiple part names.
	FmtStr pilot_name(0, 0);
	pilot_name.begin_mad_lib(16163); // ids of "%s0 %s1"
	if (arch.Infocard != 0)
	{
		pilot_name.append_string(arch.Infocard);
		if (arch.Infocard2 != 0)
		{
			pilot_name.append_string(arch.Infocard2);
		}
	}
	else
	{
		pilot_name.append_string(rand_name());  // ids that replaces %s0
		pilot_name.append_string(rand_name()); // ids that replaces %s1
	}
	pilot_name.end_mad_lib();

	pub::Reputation::Alloc(si.iRep, scanner_name, pilot_name);
	pub::Reputation::SetAffiliation(si.iRep, arch.IFF);

	uint iSpaceObj;

	pub::SpaceObj::Create(iSpaceObj, si);

	pub::AI::SetPersonalityParams pers = HkMakePersonality(arch.Graph, arch.PilotNickname);
	pub::AI::SubmitState(iSpaceObj, &pers);

	npcs.insert(iSpaceObj);
	npcsGroups[groupName].push_back(iSpaceObj);

	return;
}

void CreateNPC(const wstring& name, Vector pos, Matrix& rot, uint iSystem, const wstring& groupName, const wstring& coordName)
{
	NPC_ARCHTYPESSTRUCT arch = mapNPCArchtypes[name];
	CreateNPC(arch, pos, rot, iSystem, groupName, coordName);
	return;
}

void ShowPlayerMissionText(uint iClientID, const wstring& text)
{
	HkChangeIDSString(iClientID, 526999, text);

	FmtStr caption(0, 0);
	caption.begin_mad_lib(526999);
	caption.end_mad_lib();

	pub::Player::DisplayMissionMessage(iClientID, caption, MissionMessageType::MissionMessageType_Type2, true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Client command processing
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AdminCmd_EncounterTest(CCmds* cmds)
{
	cmds->Print(L"OK\n");
	uint spawningPlayer = HkGetClientIdFromCharname(cmds->GetAdminName());

	uint iShip1;
	pub::Player::GetShip(spawningPlayer, iShip1);
	if (!iShip1)
		return;

	uint iSystem;
	pub::Player::GetSystem(spawningPlayer, iSystem);

	Vector pos = { 20000, 0, -20000 };
	Matrix rot = EulerMatrix({ 0, 0, 0 });

	NPC_ARCHTYPESSTRUCT linerStruct;

	linerStruct.Shiparch = CreateID("dsy_prison");
	linerStruct.Loadout = CreateID("prison_liner_li");
	linerStruct.PilotNickname = "gunboat_default";

	// IFF calc
	pub::Reputation::GetReputationGroup(linerStruct.IFF, "li_p_grp");

	// Selected graph
	linerStruct.Graph = 2;

	// Infocard
	linerStruct.Infocard = 196977;

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<unsigned int> distr(202608, 202647);
	
	linerStruct.Infocard2 = distr(gen);

	CreateNPC(linerStruct, pos, rot, iSystem, L"encounter_liner", L"");

	Vector rogue_pos = { 15000, 0, -20000 };
	Matrix rogue_rot = EulerMatrix({ 0, 0, 0 });

	std::uniform_int_distribution<unsigned int> distr2(227308, 227410);
	std::uniform_int_distribution<unsigned int> distr3(227708, 228007);

	for (int i = 0; i < 4; i++)
	{
		NPC_ARCHTYPESSTRUCT rogueStruct;
		rogueStruct.Shiparch = CreateID("dsy_kadesh_hf");
		rogueStruct.Loadout = CreateID("gd_z_hf_mid_l1_d10_d13");

		pub::Reputation::GetReputationGroup(rogueStruct.IFF, "fc_pirate");

		rogueStruct.Graph = 0;
		rogueStruct.PilotNickname = "pilot_lrogue_hard";

		rogueStruct.Infocard = distr2(gen);
		rogueStruct.Infocard2 = distr3(gen);

		CreateNPC(rogueStruct, rogue_pos, rogue_rot, iSystem, L"encounter_rogues", L"");
	}

	for (int i = 0; i < 2; i++)
	{
		NPC_ARCHTYPESSTRUCT rogueStruct;
		rogueStruct.Shiparch = CreateID("dsy_zon_vhf");
		rogueStruct.Loadout = CreateID("gd_z_zonervhf_elite_l1_d19");

		pub::Reputation::GetReputationGroup(rogueStruct.IFF, "fc_pirate");

		rogueStruct.Graph = 0;
		rogueStruct.PilotNickname = "pilot_lrogue_ace";

		rogueStruct.Infocard = distr2(gen);
		rogueStruct.Infocard2 = distr3(gen);

		CreateNPC(rogueStruct, rogue_pos, rogue_rot, iSystem, L"encounter_rogues", L"");
	}

	uint linerID;
	const auto& liner = npcsGroups.at(L"encounter_liner");
	for (uint npc : liner)
	{
		linerID = npc;
	}

	const auto& rogues = npcsGroups.at(L"encounter_rogues");
	for (uint npc : rogues)
	{
		pub::AI::DirectiveCancelOp cancelOP;
		pub::AI::SubmitDirective(npc, &cancelOP);

		pub::AI::DirectiveGotoOp go;
		go.iGotoType = 0;
		go.goto_cruise = true;
		go.iTargetID = linerID;
		go.fRange = 1000;
		pub::AI::SubmitDirective(npc, &go);
	}

	encountersToInit.insert(L"encounter_liner");

	return;
}

void AdminCmd_AIMake(CCmds* cmds, int Amount, const wstring& NpcType, const wstring& groupName, const wstring& coordName)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		if (Amount == 0) { Amount = 1; }

	NPC_ARCHTYPESSTRUCT arch;

	bool wrongnpcname = 0;

	map<wstring, NPC_ARCHTYPESSTRUCT>::iterator iter = mapNPCArchtypes.find(NpcType);
	if (iter != mapNPCArchtypes.end())
	{
		arch = iter->second;
	}
	else
	{
		cmds->Print(L"ERR Wrong NPC name\n");
		return;
	}

	uint iShip1;
	pub::Player::GetShip(HkGetClientIdFromCharname(cmds->GetAdminName()), iShip1);
	if (!iShip1)
		return;

	uint iSystem;
	pub::Player::GetSystem(HkGetClientIdFromCharname(cmds->GetAdminName()), iSystem);

	Vector pos;
	Matrix rot;
	pub::SpaceObj::GetLocation(iShip1, pos, rot);

	//Creation counter
	for (int i = 0; i < Amount; i++)
	{
		if(coordName != L"")
		CreateNPC(NpcType, pos, rot, iSystem, groupName, coordName);
		Log_CreateNPC(NpcType);
	}

	return;
}

void AdminCmd_AIKill(CCmds* cmds, const wstring& groupName)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		if (groupName == L"all")
		{
			for (uint npc : npcs)
			{
				pub::SpaceObj::Destroy(npc, DestroyType::VANISH);
			}
			npcs.clear();
			cmds->Print(L"OK\n");
		}
		else if (npcsGroups.count(groupName))
		{
			const auto& fleet = npcsGroups.at(groupName);
			for (uint npc : fleet)
			{
				pub::SpaceObj::Destroy(npc, DestroyType::VANISH);
				npcs.erase(npc);
			}
			npcsGroups.erase(groupName);
		}
		else
		{
			cmds->Print(L"Invalid group name provided\n");
		}

	return;
}

/* Make AI come to your position */
void AdminCmd_AICome(CCmds* cmds, const wstring& groupName)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		uint iShip1;
	pub::Player::GetShip(HkGetClientIdFromCharname(cmds->GetAdminName()), iShip1);
	if (!iShip1)
	{
		return;
	}
	Vector pos;
	Matrix rot;
	pub::SpaceObj::GetLocation(iShip1, pos, rot);

	if (groupName == L"all")
	{
		for (uint ship : npcs)
		{
			pub::AI::DirectiveCancelOp cancelOP;
			pub::AI::SubmitDirective(ship, &cancelOP);

			pub::AI::DirectiveGotoOp go;
			go.iGotoType = 1;
			go.vPos = pos;
			go.vPos.x = pos.x + rand_FloatRange(0, 500);
			go.vPos.y = pos.y + rand_FloatRange(0, 500);
			go.vPos.z = pos.z + rand_FloatRange(0, 500);
			go.fRange = 0;
			pub::AI::SubmitDirective(ship, &go);
		}
		cmds->Print(L"OK\n");
	}
	else if (npcsGroups.count(groupName))
	{
		const auto& fleet = npcsGroups.at(groupName);
		for (uint ship : fleet)
		{
			pub::AI::DirectiveCancelOp cancelOP;
			pub::AI::SubmitDirective(ship, &cancelOP);

			pub::AI::DirectiveGotoOp go;
			go.iGotoType = 1;
			go.vPos = pos;
			go.vPos.x = pos.x + rand_FloatRange(0, 500);
			go.vPos.y = pos.y + rand_FloatRange(0, 500);
			go.vPos.z = pos.z + rand_FloatRange(0, 500);
			go.fRange = 0;
			pub::AI::SubmitDirective(ship, &go);
		}
		cmds->Print(L"OK\n");
	}
	else
	{
		cmds->Print(L"Invalid group name provided\n");
	}
	return;
}

/* Make AI follow you until death */
void AdminCmd_AIFollow(CCmds* cmds, const wstring& wscCharname, const wstring& groupName)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		HKPLAYERINFO info;
	if (HkGetPlayerInfo(wscCharname, info, false) != HKE_OK)
	{
		cmds->Print(L"ERR Player not found\n");
		return;
	}

	uint iShip1;
	pub::Player::GetShip(info.iClientID, iShip1);
	if (!iShip1)
	{
		cmds->Print(L"ERR Player not in space\n");
		return;
	}

	if (groupName == L"all")
	{
		for (uint npc : npcs)
		{
			pub::AI::DirectiveCancelOp cancelOP;
			pub::AI::SubmitDirective(npc, &cancelOP);
			pub::AI::DirectiveFollowOp testOP;
			testOP.leader = iShip1;
			testOP.max_distance = 100;
			pub::AI::SubmitDirective(npc, &testOP);
		}
		cmds->Print(L"Following %s\n", info.wscCharname.c_str());
	}
	else if (npcsGroups.count(groupName))
	{
		const auto& fleet = npcsGroups.at(groupName);
		for (uint npc : fleet)
		{
			pub::AI::DirectiveCancelOp cancelOP;
			pub::AI::SubmitDirective(npc, &cancelOP);
			pub::AI::DirectiveFollowOp testOP;
			testOP.leader = iShip1;
			testOP.max_distance = 100;
			pub::AI::SubmitDirective(npc, &testOP);
		}
		cmds->Print(L"Following %s\n", info.wscCharname.c_str());
	}
	else
	{
		cmds->Print(L"Invalid group name provided\n");
	}
	return;
}

/* Cancel the current operation */
void AdminCmd_AICancel(CCmds* cmds, const wstring& groupName)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		uint iShip1;
	pub::Player::GetShip(HkGetClientIdFromCharname(cmds->GetAdminName()), iShip1);
	if (!iShip1)
	{
		return;
	}

	if (groupName == L"all")
	{
		for (uint npc : npcs)
		{
			pub::AI::DirectiveCancelOp testOP;
			pub::AI::SubmitDirective(npc, &testOP);
		}
		cmds->Print(L"OK\n");
	}
	else if (npcsGroups.count(groupName))
	{
		const auto& fleet = npcsGroups.at(groupName);
		for (uint npc : fleet)
		{
			pub::AI::DirectiveCancelOp testOP;
			pub::AI::SubmitDirective(npc, &testOP);
		}
		cmds->Print(L"OK\n");
	}
	else
	{
		cmds->Print(L"Invalid group name provided\n");
	}

	return;
}

void AdminCmd_AIGoto(CCmds* cmds, const wstring& groupName, const wstring& coordName, bool useCruise)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		const auto& coords = coordList.find(coordName);
	if (coords == coordList.end())
	{
		cmds->Print(L"Coordinates not provided\n");
		return;
	}

	if (groupName == L"all")
	{
		for (uint npc : npcs)
		{
			pub::AI::DirectiveCancelOp cancelOP;
			pub::AI::SubmitDirective(npc, &cancelOP);

			pub::AI::DirectiveGotoOp go;
			go.iGotoType = 1;
			go.vPos = coords->second.pos;
			go.vPos.x += rand_FloatRange(-coords->second.spread, coords->second.spread);
			go.vPos.y += rand_FloatRange(-coords->second.spread, coords->second.spread);
			go.vPos.z += rand_FloatRange(-coords->second.spread, coords->second.spread);
			go.fRange = 0;
			if (useCruise)
			{
				go.goto_cruise = true;
			}
			else
			{
				go.goto_no_cruise = true;
			}
			pub::AI::SubmitDirective(npc, &go);
		}
		cmds->Print(L"OK\n");
	}
	else if (npcsGroups.count(groupName))
	{
		const auto& fleet = npcsGroups.at(groupName);
		for (uint npc : fleet)
		{
			pub::AI::DirectiveCancelOp cancelOP;
			pub::AI::SubmitDirective(npc, &cancelOP);

			pub::AI::DirectiveGotoOp go;
			go.iGotoType = 1;
			go.vPos = coords->second.pos;
			go.vPos.x += rand_FloatRange(-coords->second.spread, coords->second.spread);
			go.vPos.y += rand_FloatRange(-coords->second.spread, coords->second.spread);
			go.vPos.z += rand_FloatRange(-coords->second.spread, coords->second.spread);
			go.fRange = 0;
			if (useCruise)
			{
				go.goto_cruise = true;
			}
			else
			{
				go.goto_no_cruise = true;
			}
			pub::AI::SubmitDirective(npc, &go);
		}
		cmds->Print(L"OK\n");
	}
	else
	{
		cmds->Print(L"Invalid group name provided\n");
	}
}

void AdminCmd_ListNPCGroups(CCmds* cmds)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		cmds->Print(L"Fleets spawned: %u\n", npcsGroups.size());
	for (const auto& npcGroup : npcsGroups)
	{
		cmds->Print(L"%ls: %u ships\n", npcGroup.first.c_str(), npcGroup.second.size());
	}
}

/** List npc fleets */
void AdminCmd_ListNPCFleets(CCmds* cmds)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		cmds->Print(L"Available fleets: %d\n", mapNPCFleets.size());
	for (map<wstring, NPC_FLEETSTRUCT>::iterator i = mapNPCFleets.begin();
		i != mapNPCFleets.end(); ++i)
	{
		cmds->Print(L"|%s\n", i->first.c_str());
	}
	cmds->Print(L"OK\n");

	return;
}


/* Spawn a Fleet */
void AdminCmd_AIFleet(CCmds* cmds, const wstring& FleetName, const wstring& coordsName)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		map<wstring, NPC_FLEETSTRUCT>::iterator iter = mapNPCFleets.find(FleetName);
	if (iter != mapNPCFleets.end())
	{
		NPC_FLEETSTRUCT& fleetmembers = iter->second;
		for (map<wstring, int>::iterator i = fleetmembers.fleetmember.begin(); i != fleetmembers.fleetmember.end(); ++i)
		{
			wstring membername = i->first;
			int amount = i->second;

			AdminCmd_AIMake(cmds, amount, membername, FleetName, coordsName);
		}
		cmds->Print(L"OK fleet spawned\n");
	}
	else
	{
		cmds->Print(L"ERR Wrong Fleet name\n");
	}
}

void AdminCmd_SetCoordsHere(CCmds* cmds, const wstring& coordName, float spread)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

	if (coordList.count(coordName))
	{
		cmds->Print(L"Coordinates already defined!\n");
		return;
	}

	uint iShip1;
	pub::Player::GetShip(HkGetClientIdFromCharname(cmds->GetAdminName()), iShip1);
	if (!iShip1)
	{
		cmds->Print(L"Not in space!\n");
		return;
	}
	Vector pos;
	Matrix rot;
	pub::SpaceObj::GetLocation(iShip1, pos, rot);

	COORDS newCoords{ pos, rot, spread };
	coordList[coordName] = newCoords;
}

void AdminCmd_SetCoords(CCmds* cmds, const wstring& coordName, Vector& pos, Vector& ori, float spread)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		if (coordList.count(coordName))
		{
			cmds->Print(L"Coordinates already defined!\n");
			return;
		}

	Matrix rot = EulerMatrix(ori);

	COORDS newCoords{ pos, rot, spread };
	coordList[coordName] = newCoords;
}

void AdminCmd_ClearCoords(CCmds* cmds, const wstring& coordName)
{
	RIGHT_CHECK(RIGHT_AICONTROL)

		if (coordList.count(coordName))
		{
			coordList.erase(coordName);
		}
		else
		{
			cmds->Print(L"Coordinates not defined!\n");
		}
}

enum direction
{
	zzz = 0,
	U = 1 << 0,
	D = 1 << 1,
	W = 1 << 2,
	E = 1 << 3,
	N = 1 << 4,
	S = 1 << 5
};

void ExploreZone(CmnAsteroid::CAsteroidField* field, Vector pos, Matrix& rot, float size, direction dir)
{
	if (!field->near_field(pos))
	{
		return;
	}
	field->populate_asteroids(pos, pos);
	if (dir == zzz || !(dir & (N | E)))
	{
		Vector vecW = pos;
		TranslateX(vecW, rot, size);
		ExploreZone(field, vecW, rot, size, (direction)(dir | W));
	}
	if (dir == zzz || !(dir & (S | W)))
	{
		Vector vecE = pos;
		TranslateX(vecE, rot, -size);
		ExploreZone(field, vecE, rot, size, (direction)(dir | E));
	}
	if (dir == zzz || !(dir & (S | E)))
	{
		Vector vecN = pos;
		TranslateY(vecN, rot, size);
		ExploreZone(field, vecN, rot, size, (direction)(dir | N));
	}
	if (dir == zzz || !(dir & (N | W)))
	{
		Vector vecS = pos;
		TranslateY(vecS, rot, -size);
		ExploreZone(field, vecS, rot, size, (direction)(dir | S));
	}
	if (dir == zzz || dir == U)
	{
		Vector vecU = pos;
		TranslateZ(vecU, rot, size);
		ExploreZone(field, vecU, rot, size, U);
	}
	if (dir == zzz || dir == D)
	{
		Vector vecD = pos;
		TranslateZ(vecD, rot, -size);
		ExploreZone(field, vecD, rot, size, D);
	}
}

#define IS_CMD(a) !wscCmd.compare(L##a)

bool ExecuteCommandString_Callback(CCmds* cmds, const wstring& wscCmd)
{
	returncode = DEFAULT_RETURNCODE;
	if (IS_CMD("aicreate"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_AIMake(cmds, cmds->ArgInt(1), cmds->ArgStr(2), cmds->ArgStr(3), cmds->ArgStr(4));
		return true;
	}
	if (IS_CMD("aicreatefleet"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_AIFleet(cmds, cmds->ArgStr(1), cmds->ArgStr(2));
		return true;
	}
	else if (IS_CMD("aidestroy"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_AIKill(cmds, cmds->ArgStr(1));
		return true;
	}
	else if (IS_CMD("aicancel"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_AICancel(cmds, cmds->ArgStr(1));
		return true;
	}
	else if (IS_CMD("aifollow"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_AIFollow(cmds, cmds->ArgCharname(1), cmds->ArgStr(2));
		return true;
	}
	else if (IS_CMD("aicome"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_AICome(cmds, cmds->ArgStr(1));
		return true;
	}
	else if (IS_CMD("aigoto"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_AIGoto(cmds, cmds->ArgStr(1), cmds->ArgStr(2), cmds->ArgInt(3));
		return true;
	}
	else if (IS_CMD("listgroup"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_ListNPCGroups(cmds);
		return true;
	}
	else if (IS_CMD("fleetlist"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_ListNPCFleets(cmds);
		return true;
	}
	else if (IS_CMD("aisetcoordshere"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_SetCoordsHere(cmds, cmds->ArgStr(1), cmds->ArgFloat(2));
		return true;
	}
	else if (IS_CMD("aisetcoords"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		Vector pos{ cmds->ArgFloat(2), cmds->ArgFloat(3), cmds->ArgFloat(4) };
		Vector ori{ cmds->ArgFloat(5), cmds->ArgFloat(6), cmds->ArgFloat(7) };
		AdminCmd_SetCoords(cmds, cmds->ArgStr(1), pos, ori, cmds->ArgFloat(8));
		return true;
	}
	else if (IS_CMD("aiclearcoords"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_ClearCoords(cmds, cmds->ArgStr(1));
		return true;
	}
	else if (IS_CMD("encountertest"))
	{
		returncode = SKIPPLUGINS_NOFUNCTIONCALL;
		AdminCmd_EncounterTest(cmds);
		return true;
	}
	return false;
}


void HkTimerCheckKick()
{
	if (encountersToInit.size() > 0)
	{

		for (auto& test : encountersToInit)
		{
			const auto& liner = npcsGroups.at(test);
			uint linerID = 0;
			for (uint npc : liner)
			{
				pub::AI::DirectiveCancelOp cancelOP;
				pub::AI::SubmitDirective(npc, &cancelOP);

				pub::AI::DirectiveGotoOp go;
				go.iGotoType = 0;
				go.goto_no_cruise = true;
				go.fThrust = 50;
				go.iTargetID = CreateID("LI09_to_Li01");
				go.fRange = 0;
				pub::AI::SubmitDirective(npc, &go);
				linerID = npc;
			}
			PlayerData* pd = nullptr;

			CShip* TheLiner = (CShip*)CObject::Find(linerID, CObject::CSHIP_OBJECT);
			TheLiner->Release();

			uint spawningPlayer = 0;

			while (pd = Players.traverse_active(pd))
			{
				CShip* cship = ClientInfo[pd->iOnlineID].cship;
				if (!cship)
				{
					continue;
				}
				if (cship->system != TheLiner->system)
				{
					continue;
				}
				if (HkDistance3D(cship->vPos, TheLiner->vPos) >= 10000)
				{
					continue;
				}
				else
				{
					spawningPlayer = HkGetClientIdFromPD(pd);
					break;
				}
			}

			if (!spawningPlayer)
			{
				return;
			}

			int rep;
			int liner_rep;
			uint linerIFF;
			float attitude;
			pub::Player::GetRep(spawningPlayer, rep);
			pub::SpaceObj::GetRep(linerID, liner_rep);
			Reputation::Vibe::GetAffiliation(liner_rep, linerIFF, false);
			Reputation::Vibe::GetGroupFeelingsTowards(rep, TheLiner->get_vibe(), attitude);

			if (attitude < -0.55f)
			{
				wstring missionText = L"Destroy the Prison Liner!";
				ShowPlayerMissionText(spawningPlayer, missionText);

				uint voiceHash = CreateID("TLEADER_voice_m03");
				uint lineHash = CreateID("DX_M03_0660_TRANSPORT_LEADER");

				PlayerData* pd = nullptr;

				while (pd = Players.traverse_active(pd))
				{
					CShip* cship = ClientInfo[pd->iOnlineID].cship;
					if (!cship)
					{
						continue;
					}
					if (cship->system != TheLiner->system)
					{
						continue;
					}
					if (HkDistance3D(cship->vPos, TheLiner->vPos) >= 10000)
					{
						continue;
					}
					pub::SpaceObj::SendComm(linerID, cship->id, voiceHash, nullptr, 0, &lineHash, 9, 0, 0.0, true);
				}

				npcsToWatchEngagePlayers.insert(linerID);
			}
			else
			{
				uint voiceHash = CreateID("TLEADER_voice_m03");
				uint lineHash = CreateID("DX_M03_0664_TRANSPORT_LEADER");

				PlayerData* pd = nullptr;

				while (pd = Players.traverse_active(pd))
				{
					CShip* cship = ClientInfo[pd->iOnlineID].cship;
					if (!cship)
					{
						continue;
					}
					if (cship->system != TheLiner->system)
					{
						continue;
					}
					if (HkDistance3D(cship->vPos, TheLiner->vPos) >= 15000)
					{
						continue;
					}
					pub::SpaceObj::SendComm(linerID, cship->id, voiceHash, nullptr, 0, &lineHash, 9, 0, 0.0, false);
				}

				wstring missionText = L"Rescue the Prison Liner!";
				ShowPlayerMissionText(spawningPlayer, missionText);
			}

			npcsToWatchDamage.insert(linerID);
		}

		encountersToInit.clear();
	}

	if (npcsToWatchEngagePlayers.size() > 0)
	{
		for (auto& npc : npcsToWatchEngagePlayers)
		{
			CShip* TheLiner = (CShip*)CObject::Find(npc, CObject::CSHIP_OBJECT);
			TheLiner->Release();

			PlayerData* pd = nullptr;

			int rep;
			int liner_rep;
			uint linerIFF;
			float attitude;
			pub::SpaceObj::GetRep(TheLiner->id, liner_rep);
			Reputation::Vibe::GetAffiliation(liner_rep, linerIFF, false);
			uint TheBadGuy = 0;

			while (pd = Players.traverse_active(pd))
			{
				CShip* cship = ClientInfo[pd->iOnlineID].cship;
				pub::Player::GetRep(pd->iOnlineID, rep);
				Reputation::Vibe::GetGroupFeelingsTowards(rep, TheLiner->get_vibe(), attitude);
				if (!cship)
				{
					continue;
				}
				if (cship->system != TheLiner->system)
				{
					continue;
				}
				if (HkDistance3D(cship->vPos, TheLiner->vPos) >= 3000)
				{
					continue;
				}
				if (attitude >= -0.55f)
				{
					continue;
				}
				uint voiceHash = CreateID("TLEADER_voice_m03");
				uint lineHash = CreateID("DX_M03_0666_TRANSPORT_LEADER");

				pub::SpaceObj::SendComm(TheLiner->id, cship->id, voiceHash, nullptr, 0, &lineHash, 9, 0, 0.0, true);
			}

			npcsToWatchEngagePlayers.erase(npc);
		}
	}

	if (npcsToWatchDamage.size() > 0)
	{
		for (auto& npc : npcsToWatchDamage)
		{
			CShip* TheLiner = (CShip*)CObject::Find(npc, CObject::CSHIP_OBJECT);
			TheLiner->Release();

			if (TheLiner->get_hit_pts() / TheLiner->get_max_hit_pts() < 0.5f)
			{
				uint voiceHash = CreateID("TLEADER_voice_m03");
				uint lineHash = CreateID("DX_M03_0671_TRANSPORT_LEADER");

				PlayerData* pd = nullptr;

				while (pd = Players.traverse_active(pd))
				{
					CShip* cship = ClientInfo[pd->iOnlineID].cship;
					if (!cship)
					{
						continue;
					}
					if (cship->system != TheLiner->system)
					{
						continue;
					}
					if (HkDistance3D(cship->vPos, TheLiner->vPos) >= 15000)
					{
						continue;
					}
					pub::SpaceObj::SendComm(TheLiner->id, cship->id, voiceHash, nullptr, 0, &lineHash, 9, 0, 0.0, false);
				}

				npcsToWatchDamage.erase(npc);
			}
		}
	}

	return;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Functions to hook
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

EXPORT PLUGIN_INFO* Get_PluginInfo()
{
	PLUGIN_INFO* p_PI = new PLUGIN_INFO();
	p_PI->sName = "NPCs by Alley and Cannon";
	p_PI->sShortName = "npc";
	p_PI->bMayPause = true;
	p_PI->bMayUnload = true;
	p_PI->ePluginReturnCode = &returncode;
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&LoadSettings, PLUGIN_LoadSettings, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ExecuteCommandString_Callback, PLUGIN_ExecuteCommandString_Callback, 0));
	//p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&UserCmd_Process, PLUGIN_UserCmd_Process, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&ShipDestroyed, PLUGIN_ShipDestroyed, 0));
	p_PI->lstHooks.push_back(PLUGIN_HOOKINFO((FARPROC*)&HkTimerCheckKick, PLUGIN_HkTimerCheckKick, 0));

	return p_PI;
}
