#include "LocalPlayer.h"
#include "PluginBase/HookManager.h"
#include "PluginBase/Interfaces.h"
#include "PluginBase/Player.h"
#include "Controls/StubPanel.h"

#include <client/c_baseentity.h>
#include <client/c_baseplayer.h>
#include <client/c_baseanimating.h>
#include <cdll_int.h>
#include <convar.h>
#include <debugoverlay_shared.h>
#include <vprof.h>

#include <functional>

class LocalPlayer::TickPanel final : public virtual vgui::StubPanel
{
public:
	TickPanel(std::function<void()> setFunction)
	{
		m_SetToCurrentTarget = setFunction;
	}

	void OnTick() override;
private:
	std::function<void()> m_SetToCurrentTarget;
};



LocalPlayer::LocalPlayer()
{
	m_GetLocalPlayerIndexHookID = 0;

	enabled = new ConVar("ce_localplayer_enabled", "0", FCVAR_NONE, "enable local player override", [](IConVar *var, const char *pOldValue, float flOldValue) { GetModule()->ToggleEnabled(var, pOldValue, flOldValue); });
	player = new ConVar("ce_localplayer_player", "0", FCVAR_NONE, "player index to set as the local player");
	set_current_target = new ConCommand("ce_localplayer_set_current_target", []() { GetModule()->SetToCurrentTarget(); }, "set the local player to the current spectator target", FCVAR_NONE);
	track_spec_target = new ConVar("ce_localplayer_track_spec_target", "0", FCVAR_NONE, "have the local player value track the spectator target", [](IConVar *var, const char *pOldValue, float flOldValue) { GetModule()->ToggleTrackSpecTarget(var, pOldValue, flOldValue); });
}

bool LocalPlayer::CheckDependencies()
{
	bool ready = true;

	if (!Interfaces::GetClientDLL())
	{
		PluginWarning("Required interface IBaseClientDLL for module %s not available!\n", GetModuleName());
		ready = false;
	}

	if (!Interfaces::GetEngineClient())
	{
		PluginWarning("Required interface IVEngineClient for module %s not available!\n", GetModuleName());
		ready = false;
	}

	if (!Player::IsConditionsRetrievalAvailable())
	{
		PluginWarning("Required player condition retrieval for module %s not available!\n", GetModuleName());
		ready = false;
	}

	if (!Interfaces::GetHLTVCamera())
	{
		PluginWarning("Required interface C_HLTVCamera for module %s not available!\n", GetModuleName());
		ready = false;
	}

	try
	{
		GetHooks()->GetFunc<Global_GetLocalPlayerIndex>();
	}
	catch (bad_pointer)
	{
		PluginWarning("Required function GetLocalPlayerIndex for module %s not available!\n", GetModuleName());
		ready = false;
	}

	return ready;
}

void LocalPlayer::ToggleTrackSpecTarget(IConVar *var, const char *pOldValue, float flOldValue)
{
	if (track_spec_target->GetBool())
	{
		if (!m_Panel)
			m_Panel.reset(new TickPanel(std::bind(&LocalPlayer::SetToCurrentTarget, this)));
	}
	else
	{
		if (m_Panel)
			m_Panel.reset();
	}
}

int LocalPlayer::GetLocalPlayerIndexOverride()
{
	VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_CE);
	if (enabled->GetBool() && Player::IsValidIndex(player->GetInt()))
	{
		Player* localPlayer = Player::GetPlayer(player->GetInt(), __FUNCSIG__);
		if (localPlayer)
		{
			GetHooks()->GetHook<Global_GetLocalPlayerIndex>()->SetState(Hooking::HookAction::SUPERCEDE);
			return player->GetInt();
		}
	}

	GetHooks()->GetHook<Global_GetLocalPlayerIndex>()->SetState(Hooking::HookAction::IGNORE);
	return 0;
}

void LocalPlayer::ToggleEnabled(IConVar *var, const char *pOldValue, float flOldValue)
{
	if (enabled->GetBool())
	{
		if (!m_GetLocalPlayerIndexHookID)
		{
			m_GetLocalPlayerIndexHookID = GetHooks()->GetHook<Global_GetLocalPlayerIndex>()->AddHook(std::bind(&LocalPlayer::GetLocalPlayerIndexOverride, this));
		}
	}
	else
	{
		if (m_GetLocalPlayerIndexHookID && GetHooks()->GetHook<Global_GetLocalPlayerIndex>()->RemoveHook(m_GetLocalPlayerIndexHookID, __FUNCSIG__))
			m_GetLocalPlayerIndexHookID = 0;

		Assert(!m_GetLocalPlayerIndexHookID);
	}
}

void LocalPlayer::SetToCurrentTarget()
{
	Player* localPlayer = Player::GetPlayer(Interfaces::GetEngineClient()->GetLocalPlayer(), __FUNCSIG__);

	if (localPlayer)
	{
		if (localPlayer->GetObserverMode() == OBS_MODE_FIXED ||
			localPlayer->GetObserverMode() == OBS_MODE_IN_EYE ||
			localPlayer->GetObserverMode() == OBS_MODE_CHASE)
		{
			Player* targetPlayer = Player::AsPlayer(localPlayer->GetObserverTarget());

			if (targetPlayer)
			{
				player->SetValue(targetPlayer->GetEntity()->entindex());
				return;
			}
		}
	}

	player->SetValue(Interfaces::GetEngineClient()->GetLocalPlayer());
}

#include "PluginBase/Entities.h"

static int DrawModelOverride(IClientRenderable* renderable, int flags)
{
	auto hook = GetHooks()->GetHook<IClientRenderable_DrawModel>();
	hook->SetState(Hooking::HookAction::IGNORE);

	if (!renderable)
		return 0;

	auto unknown = renderable->GetIClientUnknown();
	if (!unknown)
		return 0;

	auto networkable = unknown->GetClientNetworkable();
	if (!networkable)
		return 0;

	if (!Entities::CheckEntityBaseclass(networkable, "TFPlayer"))
		return 0;

	auto entity = unknown->GetIClientEntity();
	if (!entity)
		return 0;

	Player* player = Player::GetPlayer(entity->entindex(), __FUNCSIG__);
	if (!player)
		return 0;

	hook->SetState(Hooking::HookAction::SUPERCEDE);
	int result = hook->GetOriginal()(renderable, flags);

	char buffer[256];
	sprintf_s(buffer,
		"Flags:  %i\n"
		"Result: %i",
		flags, result);

	NDebugOverlay::Text(player->GetEyePosition() + Vector(0, 0, 10), buffer, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

	return result;
}

void LocalPlayer::TickPanel::OnTick()
{
	VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_CE);
	if (Interfaces::GetEngineClient()->IsInGame())
		m_SetToCurrentTarget();

	//static std::map<int, std::shared_ptr<IClientRenderable_DrawModel>> s_DrawModelHooks;
	//static std::unique_ptr<IClientRenderable_DrawModel> s_DrawModelHook;
	static bool s_AddedHook = false;

	for (Player* player : Player::Iterable())
	{
		if (!player)
			continue;

		char buffer[256];
#if 0
		sprintf_s(buffer, "Local player: %s", player == Player::GetLocalPlayer() ? "true" : "false");
		NDebugOverlay::Text(player->GetAbsOrigin(), buffer, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);

		sprintf_s(buffer, "ShouldDraw(): %s", player->GetBaseEntity()->ShouldDraw() ? "true" : "false");
		NDebugOverlay::Text(player->GetAbsOrigin() + Vector(0, 0, 10), buffer, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);
#endif

		auto animating = player->GetBaseAnimating();

		if (!s_AddedHook)
		{
			s_AddedHook = true;
			auto hook = GetHooks()->GetHook<IClientRenderable_DrawModel>();
			//s_DrawModelHook.reset(new IClientRenderable_DrawModel());

			hook->AttachHook(std::make_shared<IClientRenderable_DrawModel::Inner>((IClientRenderable*)animating, &IClientRenderable::DrawModel));

			hook->AddHook(std::bind(&DrawModelOverride, std::placeholders::_1, std::placeholders::_2));
		}

		//sprintf_s(buffer, "Nodraw? %s", (animating->GetEffects() & EF_NODRAW) ? "true" : "false");
		//NDebugOverlay::Text(player->GetEyePosition() + Vector(0, 0, 10), buffer, false, NDEBUG_PERSIST_TILL_NEXT_SERVER);
	}

	engine->Con_NPrintf(0, "ShouldDrawLocalPlayer: %s", HookManager::GetRawFunc_C_BasePlayer_ShouldDrawLocalPlayer() ? "true" : "false");
}