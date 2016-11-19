#include "PluginBase/Interfaces.h"

#include <client/c_baseentity.h>
#include <client/c_baseplayer.h>
#include <util_shared.h>
#include <model_types.h>

#include <engine/IEngineTrace.h>
#include <engine/ivdebugoverlay.h>
#include <engine/ivmodelinfo.h>
#include <networkvar.h>

ConVar r_visualizetraces("r_visualizetraces", "0", FCVAR_CHEAT);

bool StandardFilterRules(IHandleEntity *pHandleEntity, int fContentsMask)
{
	CBaseEntity *pCollide = EntityFromEntityHandle(pHandleEntity);

	// Static prop case...
	if (!pCollide)
		return true;

	SolidType_t solid = pCollide->GetSolid();
	const model_t *pModel = pCollide->GetModel();

	if ((Interfaces::GetModelInfoClient()->GetModelType(pModel) != mod_brush) || (solid != SOLID_BSP && solid != SOLID_VPHYSICS))
	{
		if ((fContentsMask & CONTENTS_MONSTER) == 0)
			return false;
	}

	// This code is used to cull out tests against see-thru entities
	if (!(fContentsMask & CONTENTS_WINDOW) && pCollide->IsTransparent())
		return false;

#ifdef CE_DISABLED
	// FIXME: this is to skip BSP models that are entities that can be 
	// potentially moved/deleted, similar to a monster but doors don't seem to 
	// be flagged as monsters
	// FIXME: the FL_WORLDBRUSH looked promising, but it needs to be set on 
	// everything that's actually a worldbrush and it currently isn't
	if (!(fContentsMask & CONTENTS_MOVEABLE) && (pCollide->GetMoveType() == MOVETYPE_PUSH))// !(touch->flags & FL_WORLDBRUSH) )
		return false;
#endif

	return true;
}

bool PassServerEntityFilter(const IHandleEntity *pTouch, const IHandleEntity *pPass)
{
	if (!pPass)
		return true;

	if (pTouch == pPass)
		return false;

	const CBaseEntity *pEntTouch = EntityFromEntityHandle(pTouch);
	const CBaseEntity *pEntPass = EntityFromEntityHandle(pPass);
	if (!pEntTouch || !pEntPass)
		return true;

#ifdef CE_DISABLED
	// don't clip against own missiles
	if (pEntTouch->GetOwnerEntity() == pEntPass)
		return false;

	// don't clip against owner
	if (pEntPass->GetOwnerEntity() == pEntTouch)
		return false;
#endif

	return true;
}

CTraceFilterSimple::CTraceFilterSimple(const IHandleEntity *passentity, int collisionGroup, ShouldHitFunc_t pExtraShouldHitFunc)
{
	m_pPassEnt = passentity;
	m_collisionGroup = collisionGroup;
	m_pExtraShouldHitCheckFunction = pExtraShouldHitFunc;
}
bool CTraceFilterSimple::ShouldHitEntity(IHandleEntity *pHandleEntity, int contentsMask)
{
	if (!StandardFilterRules(pHandleEntity, contentsMask))
		return false;

	if (m_pPassEnt)
	{
		if (!PassServerEntityFilter(pHandleEntity, m_pPassEnt))
			return false;
	}

	// Don't test if the game code tells us we should ignore this collision...
	CBaseEntity *pEntity = EntityFromEntityHandle(pHandleEntity);
	if (!pEntity)
		return false;
	if (!pEntity->ShouldCollide(m_collisionGroup, contentsMask))
		return false;

#ifdef CE_DISABLED
	if (pEntity && !g_pGameRules->ShouldCollide(m_collisionGroup, pEntity->GetCollisionGroup()))
		return false;
#endif

	return true;
}

Vector UTIL_YawToVector(float yaw)
{
	Vector ret;

	ret.z = 0;
	float angle = DEG2RAD(yaw);
	SinCos(angle, &ret.y, &ret.x);

	return ret;
}

float UTIL_VecToYaw(const Vector &vec)
{
	if (vec.y == 0 && vec.x == 0)
		return 0;

	float yaw = atan2(vec.y, vec.x);

	yaw = RAD2DEG(yaw);

	if (yaw < 0)
		yaw += 360;

	return yaw;
}

C_BasePlayer* C_BasePlayer::GetLocalPlayer()
{
	IClientEntityList* const entityList = Interfaces::GetClientEntityList();
	if (!entityList)
		return nullptr;

	IClientEntity* const localPlayer = entityList->GetClientEntity(Interfaces::GetEngineClient()->GetLocalPlayer());
	if (!localPlayer)
		return nullptr;

	return dynamic_cast<C_BasePlayer*>(localPlayer->GetBaseEntity());
}

void C_BasePlayer::EyeVectors(Vector *pForward, Vector *pRight, Vector *pUp)
{
#ifdef CE_DISABLED
	if (GetVehicle() != NULL)
	{
		// Cache or retrieve our calculated position in the vehicle
		CacheVehicleView();
		AngleVectors(m_vecVehicleViewAngles, pForward, pRight, pUp);
	}
	else
#endif
	{
		AngleVectors(EyeAngles(), pForward, pRight, pUp);
	}
}

void DebugDrawLine(const Vector& vecAbsStart, const Vector& vecAbsEnd, int r, int g, int b, bool test, float duration)
{
	debugoverlay->AddLineOverlay(vecAbsStart + Vector(0, 0, 0.1), vecAbsEnd + Vector(0, 0, 0.1), r, g, b, test, duration);
}

class CFlaggedEntitiesEnum : public IPartitionEnumerator
{
public:
	CFlaggedEntitiesEnum(C_BaseEntity **pList, int listMax, int flagMask)
	{
		m_pList = pList;
		m_listMax = listMax;
		m_flagMask = flagMask;
		m_count = 0;
	}
	// This gets called	by the enumeration methods with each element
	// that passes the test.
	virtual IterationRetval_t EnumElement(IHandleEntity *pHandleEntity) override
	{
		IClientEntity *pClientEntity = Interfaces::GetClientEntityList()->GetClientEntityFromHandle(pHandleEntity->GetRefEHandle());
		C_BaseEntity *pEntity = pClientEntity ? pClientEntity->GetBaseEntity() : NULL;
		if (pEntity)
		{
			if (m_flagMask && !(pEntity->GetFlags() & m_flagMask))	// Does it meet the criteria?
				return ITERATION_CONTINUE;

			if (!AddToList(pEntity))
				return ITERATION_STOP;
		}

		return ITERATION_CONTINUE;
	}

	int GetCount() { return m_count; }
	bool AddToList(C_BaseEntity *pEntity)
	{
		if (m_count >= m_listMax)
			return false;
		m_pList[m_count] = pEntity;
		m_count++;
		return true;
	}

private:
	C_BaseEntity	**m_pList;
	int				m_listMax;
	int				m_flagMask;
	int				m_count;
};

int UTIL_EntitiesInBox(C_BaseEntity **pList, int listMax, const Vector &mins, const Vector &maxs, int flagMask, int partitionMask)
{
	CFlaggedEntitiesEnum boxEnum(pList, listMax, flagMask);
	Interfaces::GetSpatialPartition()->EnumerateElementsInBox(partitionMask, mins, maxs, false, &boxEnum);

	return boxEnum.GetCount();
}

bool IsBoxIntersectingBox(const Vector& boxMin1, const Vector& boxMax1,
	const Vector& boxMin2, const Vector& boxMax2)
{
	Assert(boxMin1[0] <= boxMax1[0]);
	Assert(boxMin1[1] <= boxMax1[1]);
	Assert(boxMin1[2] <= boxMax1[2]);
	Assert(boxMin2[0] <= boxMax2[0]);
	Assert(boxMin2[1] <= boxMax2[1]);
	Assert(boxMin2[2] <= boxMax2[2]);

	if ((boxMin1[0] > boxMax2[0]) || (boxMax1[0] < boxMin2[0]))
		return false;
	if ((boxMin1[1] > boxMax2[1]) || (boxMax1[1] < boxMin2[1]))
		return false;
	if ((boxMin1[2] > boxMax2[2]) || (boxMax1[2] < boxMin2[2]))
		return false;
	return true;
}