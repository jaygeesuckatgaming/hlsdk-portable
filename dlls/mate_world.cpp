// The mate in the world: a follower NPC (a Barney) that m8 orders around, and what the game
// tells m8 about it.
//
// The follower AI is GoldSrc's own (CTalkMonster::StartFollowing): it walks, opens doors,
// fights and keeps up with the player. m8 chooses among the actions in Mate_WorldState
// (follow/stay/wait/attack), and they arrive as {"type":"action","action":...,"option":...}.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "player.h"
#include "weapons.h"
#include "talkmonster.h"

#include "mate.h"

#define MATE_NAME "Gem"
#define MATE_TARGETNAME "gem_mate"
#define MATE_MODEL_FILE "mate_model.txt"
#define MATE_RESPAWN_FILE "mate_respawn.txt"
#define MATE_DEFAULT_MODEL "models/barney.mdl"
#define MATE_PLAYER "Gordon"

static int g_mate_index = 0;
static float g_mate_spawn_at = 0.0f;
static Vector g_mate_last_origin = Vector(0.0f, 0.0f, 0.0f);
static float g_mate_stuck_at = 0.0f;
static bool g_mate_hold = false;
static char g_mate_model[160] = MATE_DEFAULT_MODEL;
static char g_mate_respawn_token[64] = "";

static CBasePlayer *Mate_Player(void)
{
	return (CBasePlayer *)UTIL_PlayerByIndex(1);
}

/* the first line of a file in the mod's folder, trimmed (or empty) */
static void Mate_ReadLine(const char *name, char *out, int size)
{
	char path[600];
	FILE *file;
	int length;

	out[0] = 0;
	{
		char dir[512];

		GET_GAME_DIR(dir);
		snprintf(path, sizeof(path), "%s\\%s", dir, name);
	}
	file = fopen(path, "r");
	if (!file)
		return;
	if (fgets(out, size, file))
	{
		length = (int)strlen(out);
		while (length > 0 && (out[length - 1] == '\n' || out[length - 1] == '\r' ||
			out[length - 1] == ' ' || out[length - 1] == '\t'))
			out[--length] = 0;
	}
	fclose(file);
}

static void Mate_ReadModel(void)
{
	char model[160];

	Mate_ReadLine(MATE_MODEL_FILE, model, sizeof(model));
	snprintf(g_mate_model, sizeof(g_mate_model), "%s", model[0] ? model : MATE_DEFAULT_MODEL);
}

/* the models the panel can pick from without a map restart (all precached); a custom model
   in mate_model.txt is precached too, but only when the map loads */
void Mate_WorldPrecache(void)
{
	static const char *models[] = {
		MATE_DEFAULT_MODEL,
		"models/scientist.mdl",
		"models/hgrunt.mdl",
		"models/gman.mdl",
		"models/player/gina/gina.mdl",
		"models/player/gordon/gordon.mdl",
	};
	int index;

	Mate_ReadModel();
	for (index = 0; index < (int)(sizeof(models) / sizeof(models[0])); index++)
		PRECACHE_MODEL((char *)models[index]);
	PRECACHE_MODEL((char *)g_mate_model);
}

static CTalkMonster *Mate_Get(void)
{
	CBaseEntity *found = NULL;
	CBaseEntity *dead = NULL;

	while ((found = UTIL_FindEntityByTargetname(found, MATE_TARGETNAME)) != NULL)
	{
		CBaseMonster *monster = found->MyMonsterPointer();

		if (!monster)
			continue;
		if (monster->IsAlive())
			return (CTalkMonster *)found;
		if (!dead)
			dead = found;
	}
	return (CTalkMonster *)dead;
}

/* (an old save can hold more than one: keep the one in use, remove the rest) */
static void Mate_Prune(CBaseEntity *keep)
{
	CBaseEntity *found = NULL;

	while ((found = UTIL_FindEntityByTargetname(found, MATE_TARGETNAME)) != NULL)
	{
		if (!keep)
			keep = found;
		else if (found != keep)
			UTIL_Remove(found);
	}
}

static void Mate_Spawn(CBasePlayer *player)
{
	edict_t *ed;
	CBaseEntity *ent;
	float forward[3];
	Vector origin;

	if (Mate_Get())
		return;
	Mate_ReadModel();
	ed = CREATE_NAMED_ENTITY(MAKE_STRING("monster_barney"));
	if (!ed)
		return;
	ent = CBaseEntity::Instance(ed);
	ent->pev->targetname = MAKE_STRING(MATE_TARGETNAME);
	g_engfuncs.pfnAngleVectors(player->pev->angles, forward, NULL, NULL);
	origin = player->pev->origin + Vector(forward[0], forward[1], 0.0f) * 48.0f
		+ Vector(0.0f, 0.0f, 8.0f);
	ent->pev->origin = origin;
	ent->pev->angles = player->pev->angles;
	DispatchSpawn(ed);
	SET_MODEL(ENT(ent->pev), (char *)g_mate_model);
	DROP_TO_FLOOR(ENT(ent->pev));
	((CTalkMonster *)ent)->StartFollowing(player);
	g_mate_index = ENTINDEX(ed);
	g_mate_last_origin = ent->pev->origin;
	g_mate_stuck_at = 0.0f;
	g_mate_hold = false;
	ALERT(at_console, "[mate] %s is here\n", MATE_NAME);
}

void Mate_WorldReset(void)
{
	g_mate_index = 0;
	g_mate_spawn_at = 0.0f;
	g_mate_last_origin = Vector(0.0f, 0.0f, 0.0f);
	g_mate_stuck_at = 0.0f;
	g_mate_hold = false;
}

/* once a frame: keep the mate in the world (spawned near the player, back 10 s after dying) */
void Mate_WorldFrame(void)
{
	CTalkMonster *mate = Mate_Get();
	CBasePlayer *player = Mate_Player();
	static float next_prune = 0.0f;
	static float next_token = 0.0f;

	if (gpGlobals->time >= next_token)
	{
		char token[64];

		next_token = gpGlobals->time + 1.0f;
		Mate_ReadLine(MATE_RESPAWN_FILE, token, sizeof(token));
		if (token[0])
		{
			if (!g_mate_respawn_token[0])
				snprintf(g_mate_respawn_token, sizeof(g_mate_respawn_token), "%s", token);
			else if (strcmp(token, g_mate_respawn_token) != 0)
			{
				snprintf(g_mate_respawn_token, sizeof(g_mate_respawn_token), "%s", token);
				if (mate)
					UTIL_Remove(mate);
				g_mate_spawn_at = 0.0f;
			}
		}
	}

	if (!player || !player->IsAlive())
		return;
	if (mate)
	{
		Vector origin;
		float moved;
		float distance;
		bool player_moving;

		if (!mate->IsAlive())
		{
			if (g_mate_spawn_at <= 0.0f)
				g_mate_spawn_at = gpGlobals->time + 10.0f;
			else if (gpGlobals->time >= g_mate_spawn_at)
			{
				UTIL_Remove(mate);
				g_mate_spawn_at = 0.0f;
			}
			return;
		}
		if (gpGlobals->time >= next_prune)
		{
			Mate_Prune(mate);
			next_prune = gpGlobals->time + 2.0f;
		}
		origin = mate->pev->origin;
		moved = (origin - g_mate_last_origin).Length();
		distance = (origin - player->pev->origin).Length();
		player_moving = player->pev->velocity.Length() > 20.0f;
		if (player_moving && moved < 2.0f)
		{
			if (g_mate_stuck_at <= 0.0f)
				g_mate_stuck_at = gpGlobals->time;
		}
		else if (moved >= 2.0f)
		{
			g_mate_stuck_at = 0.0f;
		}
		g_mate_last_origin = origin;

		/* (told to hold, but he's walking off: keep up anyway) */
		if (!g_mate_hold && !mate->IsFollowing() && distance > 500.0f)
			mate->StartFollowing(player);

		/* (left behind, or stuck on a lift or a ledge: come back to him) */
		if (mate->IsFollowing() && (distance > 1500.0f ||
			(g_mate_stuck_at > 0.0f && distance > 120.0f &&
			 gpGlobals->time - g_mate_stuck_at > 2.0f)))
		{
			float forward[3];
			Vector spot;

			g_engfuncs.pfnAngleVectors(player->pev->angles, forward, NULL, NULL);
			spot = player->pev->origin + Vector(forward[0], forward[1], 0.0f) * 64.0f;
			UTIL_SetOrigin(mate->pev, spot);
			DROP_TO_FLOOR(ENT(mate->pev));
			mate->pev->velocity = Vector(0.0f, 0.0f, 0.0f);
			mate->ClearSchedule();
			g_mate_last_origin = mate->pev->origin;
			g_mate_stuck_at = 0.0f;
		}
		return;
	}
	if (gpGlobals->time < g_mate_spawn_at)
		return;
	Mate_Spawn(player);
	if (!Mate_Get())
		g_mate_spawn_at = gpGlobals->time + 2.0f;
}

/* what m8 chose, on the game thread */
void Mate_WorldOrder(const char *json)
{
	CTalkMonster *mate = Mate_Get();
	CBasePlayer *player = Mate_Player();

	if (!json || !strstr(json, "\"type\":\"action\""))
		return;
	if (strstr(json, "\"action\":\"follow\""))
	{
		ALERT(at_console, "[mate] order: follow\n");
		g_mate_hold = false;
		if (mate && player)
			mate->StartFollowing(player);
	}
	else if (strstr(json, "\"action\":\"stay\"") || strstr(json, "\"action\":\"wait\""))
	{
		ALERT(at_console, "[mate] order: hold\n");
		if (mate)
		{
			mate->StopFollowing(TRUE);
			g_mate_hold = true;
		}
	}
	else if (strstr(json, "\"action\":\"attack\""))
	{
		const char *option = strstr(json, "\"option\":\"");
		int index = option ? atoi(option + 10) : 0;
		edict_t *target = index > 0 ? INDEXENT(index) : NULL;

		ALERT(at_console, "[mate] order: attack\n");
		g_mate_hold = false;
		/* (told to attack without a target: the nearest thing hostile to the player) */
		if (mate && (!target || target->free) && player)
		{
			CBaseEntity *found = NULL;
			float best = 2000.0f;

			target = NULL;
			while ((found = UTIL_FindEntityInSphere(found, player->pev->origin, 2000.0f)) != NULL)
			{
				CBaseMonster *monster = found->MyMonsterPointer();
				float distance;

				if (!monster || !monster->IsAlive() ||
					monster->IRelationship(player) != R_HT)
					continue;
				distance = (found->pev->origin - player->pev->origin).Length();
				if (distance < best)
				{
					best = distance;
					target = found->edict();
				}
			}
		}
		if (mate && target && !target->free)
		{
			mate->m_hEnemy = CBaseEntity::Instance(target);
			mate->m_IdealMonsterState = MONSTERSTATE_COMBAT;
			mate->SetConditions(bits_COND_NEW_ENEMY);
			mate->ClearSchedule();
		}
	}
}

/* the state JSON m8 gets: the player, the mate, the enemies, and what it may choose */
int Mate_WorldState(char *out, int size)
{
	CBasePlayer *player = Mate_Player();
	CTalkMonster *mate;
	CBaseEntity *found = NULL;
	char enemies[512] = "";
	char options[768] = "";
	char actions[1600];
	char state[1200];
	const char *weapon = "nothing";
	const char *mate_state = "not here yet";
	const char *map;
	int mate_health = 0;
	int count = 0;

	if (!player || !player->IsAlive() || !out || size <= 0)
		return 0;
	map = STRING(gpGlobals->mapname);
	if (!map || !map[0])
		return 0;

	mate = Mate_Get();
	if (player->m_pActiveItem && player->m_pActiveItem->pev)
	{
		const char *name = STRING(player->m_pActiveItem->pev->classname);

		weapon = strncmp(name, "weapon_", 7) == 0 ? name + 7 : name;
	}
	if (mate)
	{
		mate_health = (int)mate->pev->health;
		mate_state = !mate->IsAlive() ? "down" : mate->m_hEnemy != 0 ? "fighting" :
			mate->IsFollowing() ? "following" : "holding position";
	}

	while (count < 4)
	{
		const char *name;
		float distance;
		char one[200];

		found = UTIL_FindEntityInSphere(found, player->pev->origin, 1200.0f);
		if (!found)
			break;
		CBaseMonster *monster = found->MyMonsterPointer();

		if (!monster || !monster->IsAlive() || monster->IRelationship(player) != R_HT)
			continue;
		name = STRING(found->pev->classname);
		if (strncmp(name, "monster_", 8) == 0)
			name += 8;
		distance = (found->pev->origin - player->pev->origin).Length() * 0.0254f;
		snprintf(one, sizeof(one), "%s%s, %.0f m", enemies[0] ? "; " : "", name, distance);
		strncat(enemies, one, sizeof(enemies) - strlen(enemies) - 1);
		snprintf(one, sizeof(one), "%s\"%d\":\"%s, %.0f m away\"", options[0] ? "," : "",
			ENTINDEX(found->edict()), name, distance);
		strncat(options, one, sizeof(options) - strlen(options) - 1);
		count++;
	}
	if (!enemies[0])
		snprintf(enemies, sizeof(enemies), "none in sight");

	snprintf(actions, sizeof(actions),
		"{\"name\":\"follow\",\"description\":\"follows {player}\"}%s",
		options[0] ? ",{\"name\":\"attack\",\"description\":\"attacks an enemy\",\"options\":{" : "");
	if (options[0])
	{
		strncat(actions, options, sizeof(actions) - strlen(actions) - 1);
		strncat(actions, "}}", sizeof(actions) - strlen(actions) - 1);
	}

	{
		char hands[128];

		if (strcmp(weapon, "nothing") == 0)
			snprintf(hands, sizeof(hands), "with nothing in hand");
		else
			snprintf(hands, sizeof(hands), "holding a %s", weapon);
		snprintf(state, sizeof(state),
			"%s. %s has %d health and %d armor, %s. %s is %s (%d health). Enemies: %s.",
			map, MATE_PLAYER, (int)player->pev->health, (int)player->pev->armorvalue,
			hands, MATE_NAME, mate_state, mate_health, enemies);
	}
	snprintf(out, size,
		"{\"type\":\"state\",\"game\":\"Half-Life\",\"player_name\":\"%s\",\"level\":\"%s\","
		"\"in_combat\":%s,\"on_radio\":false,\"state\":\"%s\",\"actions\":[%s]}",
		MATE_PLAYER, map, count ? "true" : "false", state, actions);
	return 1;
}
