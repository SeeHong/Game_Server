#include"global.h"

extern dynObjectType_t dynObjectType_wormhole;
extern dynObjectType_t dynObjectType_waypoint;
extern mapChannelList_t *global_channelList; //20110827 @dennton

// todo1: wormhole discovery not supported yet, all wormhole are always available
// todo2: There is bug, even though there is code to avoid sending Recv_EnteredWormhole after using another wormhole, it is still sent sometimes.

typedef struct  
{
	// used to keep info about players that already received the EnteredWormhole message
	uint64 entityId; // entityID of player
	uint32 lastUpdate; // used to detect if the player left the wormhole
	bool teleportedAway; // used so we dont send client Recv_ExitedWormhole
}wormholeTriggerCheck_t;

typedef struct  
{
	uint32 waypointID; // id unique for all maps (from teleporter table)
	uint32 contextId;  // id of map where is wormhole
	//uint64 entityId;
	uint32 nameId; // index for wormholelanguage lookup
	uint8 state;
	std::vector<wormholeTriggerCheck_t> triggeredPlayers;
	uint32 updateCounter; // used to detect if players leave the wormhole
	// note: We cannot link players->wormholes in the DB by using entityIds because they are dynamically created, so we use the plain DB row IDs instead
}wormhole_t;

typedef struct
{
	uint32 waypointID; // id unique for all maps (from teleporter table)
	//uint64 entityId;
	uint32 nameId; // index for waypointlanguage lookup
	uint8 state;
	std::vector<wormholeTriggerCheck_t> triggeredPlayers;
	uint32 updateCounter; // used to detect if players leave the waypoint
	// note: We cannot link players->waypoints in the DB by using entityIds because they are dynamically created, so we use the plain DB row IDs instead
}waypoint_t;

dynObject_t* wormhole_create(mapChannel_t *mapChannel, float x, float y, float z, float orientation, uint32 waypointID, uint32 nameId, uint32 contextId)
{
	dynObject_t *dynObject = _dynamicObject_create(25408, &dynObjectType_wormhole); // local teleport maybe, just for testing
	if( !dynObject )
		return NULL;
	dynObject->stateId = 0;
	dynamicObject_setPosition(dynObject, x, y, z);
	// setup wormhole specific data
	wormhole_t* objData = (wormhole_t*)malloc(sizeof(wormhole_t));
	memset(objData, 0x00, sizeof(wormhole_t));
	new(objData) wormhole_t();
	objData->waypointID = waypointID;
	objData->nameId = nameId;
	objData->contextId = contextId;
	dynObject->objectData = objData;
	// randomize rotation
	float randomRotY = orientation;
	float randomRotX = 0.0f;
	float randomRotZ = 0.0f;
	dynamicObject_setRotation(dynObject, randomRotY, randomRotX, randomRotZ);
	cellMgr_addToWorld(mapChannel, dynObject);
	// init periodic timer
	dynamicObject_setPeriodicUpdate(mapChannel, dynObject, 0, 400); // call about twice a second
	// add wormhole to map
	mapChannel->wormholes.push_back(dynObject);
	// return object
	return dynObject;
}

void wormhole_destroy(mapChannel_t *mapChannel, dynObject_t *dynObject)
{
	// called shortly before free()
	// todo: remove wormhole from global wormhole list
	printf("wormhole_destroy not implemented\n");
}

void wormhole_appearForPlayers(mapChannel_t *mapChannel, dynObject_t *dynObject, mapChannelClient_t **playerList, sint32 playerCount)
{
	pyMarshalString_t pms;
	pym_init(&pms);
	pym_tuple_begin(&pms);
	pym_addInt(&pms, 56);
	pym_addInt(&pms, 100); // windupTime should not be zero to avoid freezing animations?
	pym_tuple_end(&pms);
	netMgr_pythonAddMethodCallRaw(playerList, playerCount, dynObject->entityId, ForceState, pym_getData(&pms), pym_getLen(&pms));
}

void wormhole_disappearForPlayers(mapChannel_t *mapChannel, dynObject_t *dynObject, mapChannelClient_t **playerList, sint32 playerCount)
{
	// called before the object is removed from player sight
}

void wormhole_playerInAreaOfEffect(mapChannel_t *mapChannel, dynObject_t *dynObject, mapChannelClient_t* client)
{
	///////////////////
	MYSQL *sqlhnd = mysql_init(NULL);
	mysql_real_connect(sqlhnd, "127.0.0.1", "infinite", "rasa", "tr_game", 3306, NULL, 0);

	mysql_query(sqlhnd, "SELECT * FROM `teleporter`");
	MYSQL_RES *confres = mysql_store_result(sqlhnd);
	int totalrows = mysql_num_rows(confres);
	int numfields = mysql_num_fields(confres);
	MYSQL_ROW row;

	// allocate teleporter data
	sint32 NumOfTeleports = (sint32)mysql_num_rows(confres);
	di_teleporterData *teleporter_DataList = (di_teleporterData*)malloc(sizeof(di_teleporterData) * NumOfTeleports);
	di_teleporterData *teleporterData = teleporter_DataList;
	//// >>>>>>>> continue in read waypoint list >>>>>>>>>>>>>>>

	wormhole_t* objData = (wormhole_t*)dynObject->objectData;
	// objData->updateCounter
	std::vector<wormholeTriggerCheck_t>::iterator itr = objData->triggeredPlayers.begin();
	while (itr != objData->triggeredPlayers.end())
	{
		if (itr->entityId == client->clientEntityId)
		{
			itr->lastUpdate = objData->updateCounter; // update counter
			return;
		}
		++itr;
	}
	// player not found, create new entry
	wormholeTriggerCheck_t newTriggerCheck = {0};
	newTriggerCheck.entityId = client->clientEntityId;
	newTriggerCheck.lastUpdate = objData->updateCounter;
	objData->triggeredPlayers.push_back(newTriggerCheck);
	// send wormhole list (Recv_EnteredWaypoint)
	//EnteredWaypoint(currentMapId, gameContextId, mapWaypointInfoList, tempWormholes, waypointTypeId, currentWaypointId = None);
	pyMarshalString_t pms;
	pym_init(&pms);
	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// arg 1
	pym_tuple_begin(&pms);
	pym_addInt(&pms, mapChannel->mapInfo->contextId); // currentMapId

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// arg 2
	pym_addInt(&pms, mapChannel->mapInfo->contextId); // gameContextId (current)

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// arg 3
	pym_list_begin(&pms); // mapWaypointInfoList
	pym_tuple_begin(&pms);
	/////////////////////////////////////////////////////////////////////////////////////// arg 3.1
	pym_addInt(&pms, mapChannel->mapInfo->contextId); // gameContextId for wormhole
	/////////////////////////////////////////////////////////////////////////////////////// arg 3.2
	pym_list_begin(&pms); // mapInstanceList
	pym_list_end(&pms);
	/////////////////////////////////////////////////////////////////////////////////////// arg 3.3
	pym_list_begin(&pms); // wormholes
	while ((row = mysql_fetch_row(confres)))
	{
		sint32 tele_id = 0;
		sint32 tele_type = 0;
		sint32 tele_posX = 0;
		sint32 tele_posY = 0;
		sint32 tele_posZ = 0;
		sint32 tele_nameId = 0;
		sint32 tele_mapId = 0;

		sscanf(row[0], "%d", &tele_id);
		sscanf(row[1], "%d", &tele_type);
		sscanf(row[3], "%d", &tele_posX);
		sscanf(row[4], "%d", &tele_posY);
		sscanf(row[5], "%d", &tele_posZ);
		sscanf(row[11], "%d", &tele_nameId);
		sscanf(row[12], "%d", &tele_mapId);
		teleporterData->type = tele_type;
		teleporterData->sx = tele_posX;
		teleporterData->sy = tele_posY;
		teleporterData->sz = tele_posZ;
		teleporterData->nameId = tele_nameId;
		teleporterData->contextId = tele_mapId;
		///////////////////
		pym_tuple_begin(&pms); // wormhole
		pym_addInt(&pms, teleporterData->nameId); // wormholeID (not to be confused with our DB wormholeID)
		//pym_addInt(&pms, wormholeObjectData->contextId); // wormholeID (not to be confused with our DB wormholeID)
		pym_tuple_begin(&pms); // pos
		pym_addInt(&pms, teleporterData->sx);
		pym_addInt(&pms, teleporterData->sy);
		pym_addInt(&pms, teleporterData->sz);
		pym_tuple_end(&pms);
		pym_addInt(&pms, 0); // contested 
		pym_tuple_end(&pms);
	}
	pym_list_end(&pms);
	pym_tuple_end(&pms);
	pym_list_end(&pms);
	//////////////////////////////////////////////////////////////////////////////////////////// arg 4
	pym_addNoneStruct(&pms); // tempWormholes
	//////////////////////////////////////////////////////////////////////////////////////////// arg 5
	pym_addInt(&pms, 2 ); // waypointTypeId (2 --> WORMHOLE)
	//////////////////////////////////////////////////////////////////////////////////////////// arg 6
	pym_addInt(&pms, objData->waypointID); // currentWormholeId
	pym_tuple_end(&pms);
	netMgr_pythonAddMethodCallRaw(&client, 1, 5, EnteredWaypoint, pym_getData(&pms), pym_getLen(&pms));
}


bool wormhole_periodicCallback(mapChannel_t *mapChannel, dynObject_t *dynObject, uint8 timerID, sint32 timePassed)
{
	wormhole_t* objData = (wormhole_t*)dynObject->objectData;
	objData->updateCounter++;
	// check for players in range
	// calculate rect of affected cells
	sint32 minX = (sint32)(((dynObject->x-8.0f) / CELL_SIZE) + CELL_BIAS);
	sint32 minZ = (sint32)(((dynObject->z-8.0f) / CELL_SIZE) + CELL_BIAS);
	sint32 maxX = (sint32)(((dynObject->x+8.0f+(CELL_SIZE-0.0001f)) / CELL_SIZE) + CELL_BIAS);
	sint32 maxZ = (sint32)(((dynObject->z+8.0f+(CELL_SIZE-0.0001f)) / CELL_SIZE) + CELL_BIAS);
	// check all cells for players
	for(sint32 ix=minX; ix<=maxX; ix++)
	{
		for(sint32 iz=minZ; iz<=maxZ; iz++)
		{
			mapCell_t *nMapCell = cellMgr_getCell(mapChannel, ix, iz);
			if( nMapCell )
			{
				if( nMapCell->ht_playerList.empty() )
					continue;
				std::vector<mapChannelClient_t*>::iterator itr = nMapCell->ht_playerList.begin();
				while (itr != nMapCell->ht_playerList.end())
				{
					mapChannelClient_t* player = itr[0];
					++itr;
					// check distance to wormhole along xz plane
					float dX = dynObject->x - player->player->actor->posX;
					float dZ = dynObject->z - player->player->actor->posZ;
					float distance = dX*dX+dZ*dZ; // pythagoras but we optimized the sqrt() away
					if( distance >= (2.2f*2.2f) )
						continue;
					// check Y distance (rough)
					float dY = dynObject->y - player->player->actor->posY;
					if( dY < 0.0f ) dY = -dY;
					if( dY >= 10.0f )
						continue;
					wormhole_playerInAreaOfEffect(mapChannel, dynObject, player);
				}			
			}
		}
	}
	// check for not updated triggerCheck entries
	std::vector<wormholeTriggerCheck_t>::iterator itr = objData->triggeredPlayers.begin();
	while (itr != objData->triggeredPlayers.end())
	{
		if (itr->lastUpdate != objData->updateCounter)
		{
			// send exit-wormhole packet
			mapChannelClient_t* client = (mapChannelClient_t*)entityMgr_get(itr->entityId);
			if( client && itr->teleportedAway == false )
			{
				// send ExitedWormhole
				pyMarshalString_t pms;
				pym_init(&pms);
				pym_tuple_begin(&pms);
				pym_tuple_end(&pms);
				netMgr_pythonAddMethodCallRaw(&client, 1, 5, ExitedWaypoint, pym_getData(&pms), pym_getLen(&pms));
			}
			// player gone, remove entry
			itr = objData->triggeredPlayers.erase(itr);
			continue;
		}
		++itr;
	}
	return true;
}

void wormhole_useObject(mapChannel_t *mapChannel, dynObject_t *dynObject, mapChannelClient_t* client, sint32 actionID, sint32 actionArg)
{
	// not used
}

void wormhole_interruptUse(mapChannel_t *mapChannel, dynObject_t *dynObject, mapChannelClient_t* client, sint32 actionID, sint32 actionArg)
{
	// not used
}


void wormhole_recv_SelectWormhole(mapChannelClient_t *client, uint8 *pyString, sint32 pyStringLen) // at the moment it's under SelectWaypoint
{
	///////////////////
	MYSQL *sqlhnd = mysql_init(NULL);
	mysql_real_connect(sqlhnd, "127.0.0.1", "infinite", "rasa", "tr_game", 3306, NULL, 0);

	mysql_query(sqlhnd, "SELECT * FROM `teleporter`");
	MYSQL_RES *confres = mysql_store_result(sqlhnd);
	int totalrows = mysql_num_rows(confres);
	int numfields = mysql_num_fields(confres);
	MYSQL_ROW row;

	// allocate teleporter data
	sint32 NumOfTeleports = (sint32)mysql_num_rows(confres);
	di_teleporterData *teleporter_DataList = (di_teleporterData*)malloc(sizeof(di_teleporterData) * NumOfTeleports);
	di_teleporterData *teleporterData = teleporter_DataList;
	//// >>>>>>>> continue in read waypoint list >>>>>>>>>>>>>>>
	printf("wormhole opened \n");
	pyUnmarshalString_t pums;
	pym_init(&pums, pyString, pyStringLen);
	if( !pym_unpackTuple_begin(&pums) )
		return;
	sint32 mapInstanceId;
	if( pym_unpack_isNoneStruct(&pums) )
		mapInstanceId = pym_unpackNoneStruct(&pums); // should not happen, seems to be a bug due to wrong Wormhole data we send
	else
		mapInstanceId = pym_unpackInt(&pums);
	sint32 wormholeName = pym_unpackInt(&pums);

	// todo1: Check if player is actually standing on a wormhole
	// todo2: Check if the player has discovered the wormhole
	// todo3: Make it so we dont ignore mapInstanceId

	// find the wormhole
	mapChannel_t* mapChannel = client->mapChannel;
	dynObject_t* wormholeObject = NULL;
	while ((row = mysql_fetch_row(confres)))
	{
		sint32 tele_id = 0;
		sint32 tele_type = 0;
		sint32 tele_posX = 0;
		sint32 tele_posY = 0;
		sint32 tele_posZ = 0;
		sint32 tele_nameId = 0;
		sint32 tele_mapId = 0;

		sscanf(row[0], "%d", &tele_id);
		sscanf(row[1], "%d", &tele_type);
		sscanf(row[3], "%d", &tele_posX);
		sscanf(row[4], "%d", &tele_posY);
		sscanf(row[5], "%d", &tele_posZ);
		sscanf(row[11], "%d", &tele_nameId);
		sscanf(row[12], "%d", &tele_mapId);
		
		///////////////////
		std::vector<dynObject_t*>::iterator itr = mapChannel->wormholes.begin();
		dynObject_t* dynObject = *itr;
		if (tele_nameId == wormholeName)
		{
			wormholeObject = dynObject;
			printf("finded");
			teleporterData->type = tele_type;
			teleporterData->sx = tele_posX;
			teleporterData->sy = tele_posY;
			teleporterData->sz = tele_posZ;
			teleporterData->nameId = tele_nameId;
			teleporterData->contextId = tele_mapId;
			break;
		}
	}

		if( wormholeObject == NULL )
		{
			printf("Recv_SelectWormhole: Wormhole not found, cannot teleport player.\n");
			return;
		}
		// mark player as teleportedAway to avoid sending ExitedWormhole
		// todo
		// add the player to the destination wormhole triggerPlayer list (so we don't send EnteredWormhole again)
		wormhole_t* wormholeObjectData = (wormhole_t*)wormholeObject->objectData;
		wormholeTriggerCheck_t newTriggerCheck = {0};
		// #################### notify telport ##################
		//---teleport within same map
		if (client->mapChannel->mapInfo->contextId == teleporterData->contextId)
		{
			printf("same map \n");
			newTriggerCheck.entityId = client->clientEntityId;
			newTriggerCheck.lastUpdate = wormholeObjectData->updateCounter;
			wormholeObjectData->triggeredPlayers.push_back(newTriggerCheck);
			// teleport the player (on the same map)
			// the packet is only sent to the teleporting player
			// this could cause problems when the destination is near enough
			// and the player does not leave the sight range of other players
			netCompressedMovement_t netMovement = { 0 };
			client->player->actor->posX = teleporterData->sx;
			client->player->actor->posY = teleporterData->sy + 0.5f;
			client->player->actor->posZ = teleporterData->sz;
			netMovement.entityId = client->player->actor->entityId;
			netMovement.posX24b = client->player->actor->posX * 256.0f;
			netMovement.posY24b = client->player->actor->posY * 256.0f;
			netMovement.posZ24b = client->player->actor->posZ * 256.0f;
			netMgr_sendEntityMovement(client->cgm, &netMovement);
			return;
		}
		//-----teleport to other map
		else if (client->mapChannel->mapInfo->contextId != teleporterData->contextId)
		{
			printf("not same map\n");
			//remove entity from old map
			//cm->removeFromMap = true;
			//remove client from all channels
			communicator_playerExitMap(client);
			//unregister player
			//communicator_unregisterPlayer(cm);
			//remove visible entity
			Thread::LockMutex(&client->cgm->cs_general);
			cellMgr_removeFromWorld(client);
			// remove from list
			for (sint32 i = 0; i < client->mapChannel->playerCount; i++)
			{
				if (client == client->mapChannel->playerList[i])
				{
					if (i == client->mapChannel->playerCount - 1)
					{
						client->mapChannel->playerCount--;
					}
					else
					{
						client->mapChannel->playerList[i] = client->mapChannel->playerList[client->mapChannel->playerCount - 1];
						client->mapChannel->playerCount--;
					}
					break;
				}
			}
			Thread::UnlockMutex(&client->cgm->cs_general);
			//entityMgr_unregisterEntity(cm->player->actor->entityId);

			//cm->cgm->mapLoadSlotId = cm->tempCharacterData->slotIndex;
			//############## map loading stuff ##############
			// send PreWonkavate (clientMethod.134)
			pyMarshalString_t pms;
			pym_init(&pms);
			pym_tuple_begin(&pms);
			pym_addInt(&pms, 0); // wonkType - actually not used by the game
			pym_tuple_end(&pms);
			netMgr_pythonAddMethodCallRaw(client->cgm, 5, 134, pym_getData(&pms), pym_getLen(&pms));
			// send Wonkavate (inputstateRouter.242)
			client->cgm->mapLoadContextId = teleporterData->contextId;
			pym_init(&pms);
			pym_tuple_begin(&pms);
			pym_addInt(&pms, teleporterData->contextId);	// gameContextId (alias mapId)
			pym_addInt(&pms, 0);	// instanceId ( not important for now )
			// find map version
			sint32 mapVersion = 0; // default = 0;
			for (sint32 i = 0; i < mapInfoCount; i++)
			{
				if (mapInfoArray[i].contextId == teleporterData->contextId)
				{
					mapVersion = mapInfoArray[i].version;
					break;
				}
			}
			pym_addInt(&pms, mapVersion);	// templateVersion ( from the map file? )
			pym_tuple_begin(&pms);  // startPosition
			pym_addFloat(&pms, teleporterData->sx); // x (todo: send as float)
			pym_addFloat(&pms, teleporterData->sy); // y (todo: send as float)
			pym_addFloat(&pms, teleporterData->sz); // z (todo: send as float)
			pym_tuple_end(&pms);
			pym_addInt(&pms, 0);	// startRotation (todo, read from db and send as float)
			pym_tuple_end(&pms);
			netMgr_pythonAddMethodCallRaw(client->cgm, 6, Wonkavate, pym_getData(&pms), pym_getLen(&pms));
			//wonkavate = buggy?
			//cm->cgm->State = GAMEMAIN_STATE_RELIEVED;
			//################## player assigning ###############
			//communicator_registerPlayer(cm);
			communicator_loginOk(client->mapChannel, client);
			communicator_playerEnterMap(client);
			//add entity to new map
			client->player->actor->posX = teleporterData->sx;
			client->player->actor->posY = teleporterData->sy + 2;
			client->player->actor->posZ = teleporterData->sz;


			client->player->controllerUser->inventory = client->inventory;
			client->player->controllerUser->mission = client->mission;
			client->tempCharacterData = client->player->controllerUser->tempCharacterData;


			//---search new mapchannel
			for (sint32 chan = 0; chan < global_channelList->mapChannelCount; chan++)
			{
				mapChannel_t *mapChannel = global_channelList->mapChannelArray + chan;
				if (mapChannel->mapInfo->contextId == teleporterData->contextId)
				{
					client->mapChannel = mapChannel;
					break;
				}
			}

			mapChannel_t *mapChannel = client->mapChannel;
			Thread::LockMutex(&client->mapChannel->criticalSection);
			mapChannel->playerList[mapChannel->playerCount] = client;
			mapChannel->playerCount++;
			hashTable_set(&mapChannel->ht_socketToClient, (uint32)client->cgm->socket, client);
			Thread::UnlockMutex(&mapChannel->criticalSection);

			cellMgr_addToWorld(client); //cellsint32roducing to player /from players
			// setCurrentContextId (clientMethod.362)
			pym_init(&pms);
			pym_tuple_begin(&pms);
			pym_addInt(&pms, teleporterData->contextId);
			pym_tuple_end(&pms);
			netMgr_pythonAddMethodCallRaw(client->cgm, 5, 362, pym_getData(&pms), pym_getLen(&pms));

			communicator_systemMessage(client, "arrived");
		}
		else
			printf("idk what's this");
	return;
}

dynObjectType_t dynObjectType_wormhole = 
{
	wormhole_destroy, // destroy
	wormhole_appearForPlayers, // appearForPlayers
	wormhole_disappearForPlayers, // disappearForPlayer
	wormhole_periodicCallback, // periodicCallback
	wormhole_useObject, // useObject
	wormhole_interruptUse
};

void addTeleport(int test)
{
	//da
	printf("da =");
}

void testteleportfunction()
{
	int planetId, teleportId, mapId = 0;
	// planetId, continentId, mapId (those are custom id's, for database only)
		// Arieki
	if (planetId == 1)
		// main hub on Arieki
		if (teleportId == 111)
			// add teleport for other hubs
			addTeleport(211);	// earth
			addTeleport(311);	// foreas
			// add teleport for other maps on foreas
			//addTeleport(111);
			addTeleport(121);
			addTeleport(131);
			addTeleport(141);
			addTeleport(151);
			addTeleport(161);
			addTeleport(171);
			// add teleports for waypoint's on curent map
			if (mapId == 1)
			{
				//addTeleport(111);
				addTeleport(112);
				addTeleport(113);
				addTeleport(114);
				addTeleport(115);
				// add more if nessessery
			}
			else if (mapId == 2)
			{
				//addTeleport(121);
				addTeleport(122);
				addTeleport(123);
				addTeleport(124);
				addTeleport(125);
				// add more if nessessery
			}
			else if (mapId == 3)
			{
				//addTeleport(131);
				addTeleport(132);
				addTeleport(133);
				addTeleport(134);
				addTeleport(135);
				// add more if nessessery
			}
			else if (mapId == 4)
			{
				//addTeleport(141);
				addTeleport(142);
				addTeleport(143);
				addTeleport(144);
				addTeleport(145);
				// add more if nessessery
			}
			else if (mapId == 5)
			{
				//addTeleport(151);
				addTeleport(152);
				addTeleport(153);
				addTeleport(154);
				addTeleport(155);
				// add more if nessessery
			}
}