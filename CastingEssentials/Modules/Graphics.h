#pragma once

#include "PluginBase/Modules.h"

#define GLOWS_ENABLE
#include <client/glow_outline_effect.h>

#include <vector>

class C_BaseEntity;
class CGlowObjectManager;
class CViewSetup;
class CMatRenderContextPtr;
class IMaterial;
class ConCommand;
class CCommand;
enum OverrideType_t;

class Graphics final : public Module<Graphics>
{
public:
	Graphics();
	~Graphics();

	ConVar* GetDebugGlowConVar() const { return ce_graphics_debug_glow; }

protected:
	void OnTick(bool inGame) override;

private:
	ConVar* ce_graphics_disable_prop_fades;
	ConVar* ce_graphics_debug_glow;
	ConVar* ce_graphics_glow_silhouettes;
	ConVar* ce_graphics_glow_intensity;
	ConVar* ce_graphics_improved_glows;
	ConVar* ce_graphics_fix_invisible_players;
	ConVar* ce_graphics_glow_l4d;

	ConVar* ce_outlines_players_override_red;
	ConVar* ce_outlines_players_override_blue;
	ConVar* ce_outlines_debug_stencil_out;
	ConVar* ce_outlines_additive;
	ConVar* ce_outlines_infill_enable;
	ConVar* ce_outlines_infill_debug;
	ConVar* ce_outlines_infill_hurt_red;
	ConVar* ce_outlines_infill_hurt_blue;
	ConVar* ce_outlines_infill_normal_direction;
	ConVar* ce_outlines_infill_buffed_red;
	ConVar* ce_outlines_infill_buffed_blue;
	ConVar* ce_outlines_infill_buffed_direction;
	ConVar* ce_outlines_infill_w2s_mode;

	ConCommand* ce_graphics_dump_shader_params;

	static bool IsDefaultParam(const char* paramName);
	static void DumpShaderParams(const CCommand& cmd);
	static int DumpShaderParamsAutocomplete(const char *partial, char commands[64][64]);

	int m_ComputeEntityFadeHook;
	unsigned char ComputeEntityFadeOveride(C_BaseEntity* entity, float minDist, float maxDist, float fadeScale);

	int m_ApplyEntityGlowEffectsHook;
	void ApplyEntityGlowEffectsOverride(CGlowObjectManager* pThis, const CViewSetup* pSetup, int nSplitScreenSlot, CMatRenderContextPtr& pRenderContext, float flBloomScale, int x, int y, int w, int h);

	int m_ForcedMaterialOverrideHook;
	void ForcedMaterialOverrideOverride(IMaterial* material, OverrideType_t overrideType);

	void DrawGlowAlways(int nSplitScreenSlot, CMatRenderContextPtr& pRenderContext) const;
	void DrawGlowOccluded(int nSplitScreenSlot, CMatRenderContextPtr& pRenderContext) const;
	void DrawGlowVisible(int nSplitScreenSlot, CMatRenderContextPtr& pRenderContext) const;

	friend class CGlowObjectManager;
	struct ExtraGlowData
	{
		ExtraGlowData();

		bool m_ShouldOverrideGlowColor;
		Vector m_GlowColorOverride;

		bool m_InfillEnabled;
		uint8_t m_StencilIndex;

		bool m_HurtInfillActive;
		Color m_HurtInfillColor;
		Vector2D m_HurtInfillRectMin;
		Vector2D m_HurtInfillRectMax;

		bool m_BuffedInfillActive;
		Color m_BuffedInfillColor;
		Vector2D m_BuffedInfillRectMin;
		Vector2D m_BuffedInfillRectMax;

		// This list is refreshed every frame and only used within a single "entry"
		// into our glow system, so it's ok to use pointers here rather than EHANDLES
		std::vector<C_BaseEntity*> m_MoveChildren;
	};
	std::vector<ExtraGlowData> m_ExtraGlowData;
	CUtlVector<CGlowObjectManager::GlowObjectDefinition_t>* m_GlowObjectDefinitions;
	const CViewSetup* m_View;


	void BuildMoveChildLists();
	ExtraGlowData* FindExtraGlowData(int entindex);

	// Returns true if the world to screen transformation resulted in a valid rectangle
	// NOTE: This returns vgui coordinates (y = 0 at top of screen)
	bool ScreenBounds(const Vector& mins, const Vector& maxs, float& screenMins, float& screenMaxs, Vector& screenWorldMins, Vector& screenWorldMaxs);
	bool BaseAnimatingScreenBounds(const VMatrix& worldToScreen, C_BaseAnimating* animating, Vector& worldMins, Vector& worldMaxs);

	//std::map<float, Vector> GetBaseAnimatingPoints

	static void PlaneThroughPoints(const Vector& p1, const Vector& p2, const Vector& p3, Vector& planeNormal);
	static void ProjectToLine(const Vector& line0, const Vector& line1, const Vector& pointIn, Vector& pointOut, float* t = nullptr);

	void WorldToScreenAng(const Vector& world, QAngle& screenAng);
	bool WorldToScreenMat(const VMatrix& worldToScreen, const Vector& world, Vector2D& screen);

	// Returns the number of intersecting points between a plane and an AABB.
	static int PlaneAABBIntersection(const VPlane& plane, const Vector& mins, const Vector& maxs, Vector intersections[6]);

	static void GetAABBCorner(const Vector& mins, const Vector& maxs, uint_fast8_t cornerIndex, Vector& corner);
	static void GetRotatedBBCorners(const Vector& origin, const QAngle& angles, const Vector& mins, const Vector& maxs, Vector corners[8]);

	std::map<float, Vector> GetBaseAnimatingPoints(C_BaseAnimating* animating);

	bool Test_PlaneHitboxesIntersect(C_BaseAnimating* animating, Vector& worldMins, Vector& worldMaxs);
	void CalcRootLineDir(const Vector& camToBBox, const Vector& camForward, const Vector& camUp, const Vector& camRight, Vector& rootLineDir);

	void BuildExtraGlowData(CGlowObjectManager* glowMgr);
	void DrawInfills(CMatRenderContextPtr& pRenderContext);
};