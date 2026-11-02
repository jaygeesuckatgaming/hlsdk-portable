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
#define MATE_PLAYER "Gordon"

static int g_mate_index = 0;
static float g_mate_spawn_at = 0.0f;
static Vector g_mate_last_origin = Vector(0.0f, 0.0f, 0.0f);
static float g_mate_stuck_at = 0.0f;

static CBasePlayer *Mate_Player(void)
{
	return (CBasePlayer *)UTIL_PlayerByIndex(1);
}

static CTalkMonster *Mate_Get(void)
{
	edict_t *ed;
	CBaseEntity *ent;

	if (g_mate_index <= 0)
		return NULL;
	ed = INDEXENT(g_mate_index);
	if (!ed || ed->free)
	{
		g_mate_index = 0;
		return NULL;
	}
	ent = CBaseEntity::Instance(ed);
	if (!ent || !ent->pev || strcmp(STRING(ent->pev->classname), "monster_barney") != 0)
	{
		g_mate_index = 0;
		return NULL;
	}
	return (CTalkMonster *)ent;
}

static void Mate_Spawn(CBasePlayer *player)
{
	edict_t *ed;
	CBaseEntity *ent;
	float forward[3];
	Vector origin;

	ed = CREATE_NAMED_ENTITY(MAKE_STRING("monster_barney"));
	if (!ed)
		return;
	ent = CBaseEntity::Instance(ed);
	g_engfuncs.pfnAngleVectors(player->pev->angles, forward, NULL, NULL);
	origin = player->pev->origin + Vector(forward[0], forward[1], 0.0f) * 48.0f
		+ Vector(0.0f, 0.0f, 8.0f);
	ent->pev->origin = origin;
	ent->pev->angles = player->pev->angles;
	DispatchSpawn(ed);
	DROP_TO_FLOOR(ENT(ent->pev));
	((CTalkMonster *)ent)->StartFollowing(player);
	g_mate_index = ENTINDEX(ed);
	g_mate_last_origin = ent->pev->origin;
	g_mate_stuck_at = 0.0f;
	ALERT(at_console, "[mate] %s is here\n", MATE_NAME);
}

void Mate_WorldReset(void)
{
	g_mate_index = 0;
	g_mate_spawn_at = 0.0f;
	g_mate_last_origin = Vector(0.0f, 0.0f, 0.0f);
	g_mate_stuck_at = 0.0f;
}

/* once a frame: keep the mate in the world (spawned near the player, back 10 s after dying) */
void Mate_WorldFrame(void)
{
	CTalkMonster *mate = Mate_Get();
	CBasePlayer *player = Mate_Player();

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
			g_mate_index = 0;
			g_mate_spawn_at = gpGlobals->time + 10.0f;
			return;
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
		if (!mate->IsFollowing() && distance > 500.0f)
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
		if (mate && player)
			mate->StartFollowing(player);
	}
	else if (strstr(json, "\"action\":\"attack\""))
	{
		const char *option = strstr(json, "\"option\":\"");
		int index = option ? atoi(option + 10) : 0;
		edict_t *target = index > 0 ? INDEXENT(index) : NULL;

		ALERT(at_console, "[mate] order: attack\n");
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
