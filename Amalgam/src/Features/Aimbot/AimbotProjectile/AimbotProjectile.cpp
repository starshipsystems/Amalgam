#include "AimbotProjectile.h"

#include "../Aimbot.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../Simulation/ProjectileSimulation/ProjectileSimulation.h"
#include "../../TickHandler/TickHandler.h"
#include "../../Visuals/Visuals.h"

//#define SPLASH_DEBUG1
//#define SPLASH_DEBUG2

std::vector<Target_t> CAimbotProjectile::GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;
	const auto iSort = Vars::Aimbot::General::TargetSelection.Value;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
	{
		EGroupType groupType = EGroupType::PLAYERS_ENEMIES;
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_CROSSBOW: groupType = EGroupType::PLAYERS_ALL; break;
		case TF_WEAPON_LUNCHBOX: groupType = EGroupType::PLAYERS_TEAMMATES; break;
		}

		for (auto pEntity : H::Entities.GetGroup(groupType))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum() && pEntity->As<CTFPlayer>()->m_iHealth() >= pEntity->As<CTFPlayer>()->GetMaxHealth())
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.push_back({ pEntity, ETargetType::Player, vPos, vAngleTo, flFOVTo, flDistTo, F::AimbotGlobal.GetPriority(pEntity->entindex()) });
		}
	}

	if (Vars::Aimbot::General::Target.Value)
	{
		bool bIsRescueRanger = pWeapon->GetWeaponID() == TF_WEAPON_SHOTGUN_BUILDING_RESCUE;
		for (auto pEntity : H::Entities.GetGroup(bIsRescueRanger ? EGroupType::BUILDINGS_ALL : EGroupType::BUILDINGS_ENEMIES))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			if (pEntity->m_iTeamNum() == pLocal->m_iTeamNum() && pEntity->As<CBaseObject>()->m_iHealth() >= pEntity->As<CBaseObject>()->m_iMaxHealth())
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.push_back({ pEntity, pEntity->IsSentrygun() ? ETargetType::Sentry : pEntity->IsDispenser() ? ETargetType::Dispenser : ETargetType::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Stickies)
	{
		bool bShouldAim = false;
		switch (pWeapon->m_iItemDefinitionIndex())
		{
		case Demoman_s_TheQuickiebombLauncher:
		case Demoman_s_TheScottishResistance:
		case Pyro_s_TheScorchShot:
			bShouldAim = true;
		}

		if (bShouldAim)
		{
			for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_PROJECTILES))
			{
				if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
					continue;

				Vec3 vPos = pEntity->GetCenter();
				Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
				float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
				if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
					continue;

				float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
				vTargets.push_back({ pEntity, ETargetType::Sticky, vPos, vAngleTo, flFOVTo, flDistTo });
			}
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs) // does not predict movement
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_NPC))
		{
			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.push_back({ pEntity, ETargetType::NPC, vPos, vAngleTo, flFOVTo, flDistTo });
		}
	}

	return vTargets;
}

std::vector<Target_t> CAimbotProjectile::SortTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	auto vTargets = GetTargets(pLocal, pWeapon);

	F::AimbotGlobal.SortTargets(&vTargets, Vars::Aimbot::General::TargetSelection.Value);
	vTargets.resize(std::min(size_t(Vars::Aimbot::General::MaxTargets.Value), vTargets.size()));
	F::AimbotGlobal.SortPriority(&vTargets);
	return vTargets;
}



static inline float GetSplashRadius(CTFWeaponBase* pWeapon, CTFPlayer* pLocal = nullptr)
{
	float flRadius = 0.f;
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
		flRadius = 146.f;
	}
	if (!flRadius && pWeapon->m_iItemDefinitionIndex() == Pyro_s_TheScorchShot)
		flRadius = 110.f;
	if (!flRadius)
		return 0.f;

	flRadius = SDK::AttribHookValue(flRadius, "mult_explosion_radius", pWeapon);
	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_ROCKETLAUNCHER:
	case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
	case TF_WEAPON_PARTICLE_CANNON:
		if (pLocal->InCond(TF_COND_BLASTJUMPING) && SDK::AttribHookValue(1.f, "rocketjump_attackrate_bonus", pWeapon) != 1.f)
			flRadius *= 0.8f;
	}
	return flRadius * Vars::Aimbot::Projectile::SplashRadius.Value / 100;
}

static inline float PrimeTime(CTFWeaponBase* pWeapon)
{
	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::UsePrimeTime && pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		static auto tf_grenadelauncher_livetime = U::ConVars.FindVar("tf_grenadelauncher_livetime");
		const float flLiveTime = tf_grenadelauncher_livetime ? tf_grenadelauncher_livetime->GetFloat() : 0.8f;
		return SDK::AttribHookValue(flLiveTime, "sticky_arm_time", pWeapon);
	}

	return 0.f;
}

int CAimbotProjectile::GetHitboxPriority(int nHitbox, Target_t& target, Info_t& tInfo)
{
	bool bHeadshot = target.m_TargetType == ETargetType::Player && tInfo.m_pWeapon->GetWeaponID() == TF_WEAPON_COMPOUND_BOW;
	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::BodyaimIfLethal && bHeadshot)
	{
		float charge = I::GlobalVars->curtime - tInfo.m_pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime();
		float damage = Math::RemapValClamped(charge, 0.f, 1.f, 50.f, 120.f);
		if (tInfo.m_pLocal->IsMiniCritBoosted())
			damage *= 1.36f;
		if (damage >= target.m_pEntity->As<CTFPlayer>()->m_iHealth())
			bHeadshot = false;

		if (tInfo.m_pLocal->IsCritBoosted()) // for reliability
			bHeadshot = false;
	}
	const bool bLower = target.m_TargetType == ETargetType::Player && target.m_pEntity->As<CTFPlayer>()->IsOnGround() && tInfo.m_flRadius;

	if (bHeadshot)
		target.m_nAimedHitbox = HITBOX_HEAD;

	switch (nHitbox)
	{
	case 0: return bHeadshot ? 0 : 2; // head
	case 1: return bHeadshot ? 3 : (bLower ? 1 : 0); // body
	case 2: return bHeadshot ? 3 : (bLower ? 0 : 1); // feet
	}

	return 3;
};

std::unordered_map<int, Vec3> CAimbotProjectile::GetDirectPoints(Target_t& target, Info_t& tInfo)
{
	std::unordered_map<int, Vec3> mPoints = {};

	const Vec3 vMins = target.m_pEntity->m_vecMins(), vMaxs = target.m_pEntity->m_vecMaxs();
	for (int i = 0; i < 3; i++)
	{
		const int iPriority = GetHitboxPriority(i, target, tInfo);
		if (iPriority == 3)
			continue;

		switch (i)
		{
		case 0:
			if (target.m_nAimedHitbox == HITBOX_HEAD)
			{
				Vec3 vOff = target.m_pEntity->As<CBaseAnimating>()->GetHitboxOrigin(HITBOX_HEAD) - target.m_pEntity->GetAbsOrigin();

				// https://www.youtube.com/watch?v=_PSGD-pJUrM, might be better??
				//Vec3 vCenter, vBBoxMins, vBBoxMaxs; target.m_pEntity->As<CBaseAnimating>()->GetHitboxInfo(HITBOX_HEAD, &vCenter, &vBBoxMins, &vBBoxMaxs);
				//Vec3 vOff = vCenter + (vBBoxMins + vBBoxMaxs) / 2 - target.m_pEntity->GetAbsOrigin();

				vOff.x = std::clamp(vOff.x, vMins.x + Vars::Aimbot::Projectile::VerticalShift.Value, vMaxs.x - Vars::Aimbot::Projectile::VerticalShift.Value);
				vOff.y = std::clamp(vOff.y, vMins.y + Vars::Aimbot::Projectile::VerticalShift.Value, vMaxs.y - Vars::Aimbot::Projectile::VerticalShift.Value);
				vOff.z = vOff.z + (vMaxs.z - vOff.z) * (Vars::Aimbot::Projectile::HuntsmanLerp.Value / 100.f);
				vOff.z = std::clamp(vOff.z, vMins.z + Vars::Aimbot::Projectile::VerticalShift.Value, vMaxs.z - Vars::Aimbot::Projectile::VerticalShift.Value);
				mPoints[iPriority] = vOff;
			}
			else
				mPoints[iPriority] = Vec3(0, 0, vMaxs.z - Vars::Aimbot::Projectile::VerticalShift.Value);
			break;
		case 1: mPoints[iPriority] = Vec3(0, 0, (vMaxs.z - vMins.z) / 2); break;
		case 2: mPoints[iPriority] = Vec3(0, 0, vMins.z + Vars::Aimbot::Projectile::VerticalShift.Value); break;
		}
	}

	return mPoints;
}

// seode
static inline std::vector<std::pair<Vec3, int>> ComputeSphere(float flRadius, int iSamples, float flNthroot = 1.f)
{
	std::vector<std::pair<Vec3, int>> vPoints;
	vPoints.reserve(iSamples);

	for (int n = 0; n < iSamples; n++)
	{
		float flA = acosf(1.f - 2.f * n / iSamples);
		float flB = PI * (3.f - sqrtf(5.f)) * n;

		Vec3 vPoint = Vec3(sinf(flA) * cosf(flB), sinf(flA) * sinf(flB), -cosf(flA));
		if (flNthroot != 1.f)
		{
			vPoint.x = powf(fabsf(vPoint.x), 1 / flNthroot) * sign(vPoint.x);
			vPoint.y = powf(fabsf(vPoint.y), 1 / flNthroot) * sign(vPoint.y);
			vPoint.z = powf(fabsf(vPoint.z), 1 / flNthroot) * sign(vPoint.z);
			vPoint.Normalize();
		}
		vPoint *= flRadius;

		vPoints.push_back({ vPoint, Vars::Aimbot::Projectile::SplashGrates.Value ? PointType_Regular | PointType_Obscured : PointType_Regular });
	}

	return vPoints;
};

// possibly add air splash for autodet weapons
std::vector<Point_t> CAimbotProjectile::GetSplashPoints(Target_t& target, std::vector<std::pair<Vec3, int>>& vSpherePoints, Info_t& tInfo, int iSimTime)
{
	std::vector<std::pair<Point_t, float>> vPointDistances = {};

	Vec3 vTargetEye = target.m_vPos + tInfo.m_vTargetEye;

#ifndef SPLASH_DEBUG1
	auto checkPoint = [&](CGameTrace& trace, bool& bErase, bool& bNormal)
#else
	auto checkPoint = [&](CGameTrace& trace, bool& bErase, bool& bNormal, Vec3* pFrom = nullptr)
#endif
		{
			bErase = trace.fraction == 1.f || !trace.m_pEnt || !trace.m_pEnt->GetAbsVelocity().IsZero() || trace.surface.flags & 0x0004 /*SURF_SKY*/;
#ifdef SPLASH_DEBUG1
			if (pFrom && bErase)
				G::LineStorage.push_back({ { *pFrom, trace.endpos }, I::GlobalVars->curtime + 60.f, Vars::Colors::Halloween.Value });
#endif
			if (bErase)
				return false;

			Point_t tPoint = { trace.endpos, {} };
			CalculateAngle(tInfo.m_vLocalEye, tPoint.m_vPoint, tInfo, iSimTime, tPoint.m_Solution);
			Vec3 vForward;
			if (!tInfo.m_flGravity)
				Math::AngleVectors({ tPoint.m_Solution.m_flPitch, tPoint.m_Solution.m_flYaw, 0.f }, &vForward);
			else
			{
				Vec3 vPos = tInfo.m_vLocalEye + Vec3(0, 0, (tInfo.m_flGravity * 800.f * pow(tPoint.m_Solution.m_flTime, 2)) / 2);
				Math::AngleVectors(Math::CalcAngle(vPos, tPoint.m_vPoint), &vForward);
			}
			bNormal = vForward.Dot(trace.plane.normal) >= 0;
#ifdef SPLASH_DEBUG1
			if (pFrom)
			{
				G::LineStorage.push_back({ { *pFrom, trace.endpos }, I::GlobalVars->curtime + 60.f, !bNormal ? Vars::Colors::TeamBlu.Value : Vars::Colors::TeamRed.Value });
				G::LineStorage.push_back({ { trace.endpos, trace.endpos - vForward * 10 }, I::GlobalVars->curtime + 60.f, Vars::Colors::Local.Value });
			}
#endif
			if (bNormal)
				return false;

			bErase = tPoint.m_Solution.m_iCalculated == 1;
			if (!bErase || int(tPoint.m_Solution.m_flTime / TICK_INTERVAL) + 1 != iSimTime)
				return false;

			vPointDistances.push_back({ tPoint, tPoint.m_vPoint.DistTo(target.m_vPos) });
			return true;
		};
	for (auto it = vSpherePoints.begin(); it != vSpherePoints.end();)
	{
		Vec3 vPoint = it->first + vTargetEye;
		int& iType = it->second;

		Solution_t solution; CalculateAngle(tInfo.m_vLocalEye, vPoint, tInfo, iSimTime, solution, false);
		if (solution.m_iCalculated == 3)
			iType = 0;
		else if (abs(solution.m_flTime - TICKS_TO_TIME(iSimTime)) < tInfo.m_flRadiusTime)
		{
			CGameTrace trace = {};
			CTraceFilterWorldAndPropsOnly filter = {};

			if (iType & PointType_Regular)
			{
				bool bErase = false, bNormal = false;

				SDK::Trace(vTargetEye, vPoint, MASK_SOLID, &filter, &trace);
#ifndef SPLASH_DEBUG1
				checkPoint(trace, bErase, bNormal);
#else
				checkPoint(trace, bErase, bNormal, &vTargetEye);
#endif

				if (bErase)
					iType = 0;
				else if (bNormal)
					iType &= ~PointType_Regular;
				else
					iType &= ~PointType_Obscured;
			}
			if (iType & PointType_Obscured)
			{
				bool bErase = false, bNormal = false;

				if (!tInfo.m_bRocket) // just do this, it's less expensive
				{
					SDK::Trace(vPoint, vTargetEye, MASK_SHOT, &filter, &trace);
					bNormal = trace.DidHit();
					if (!bNormal)
					{
						SDK::Trace(vPoint, vTargetEye, MASK_SOLID, &filter, &trace);
						checkPoint(trace, bErase, bNormal);
					}
				}
				else // currently experimental, there may be a more efficient way to do this?
				{
					SDK::Trace(vPoint, vTargetEye, MASK_SOLID, &filter, &trace);
					bNormal = trace.fraction == 1.f /*|| trace.startsolid*/;
					if (!bNormal)
					{
						/*
						CGameTrace checkTrace = {};
						SDK::Trace(trace.endpos + trace.plane.normal, MASK_SHOT, &filter, &checkTrace);
						if (checkTrace.fraction == 1.f)
							checkPoint(trace, bErase, bNormal);
						*/

						if (checkPoint(trace, bErase, bNormal))
						{
							SDK::Trace(trace.endpos + trace.plane.normal, vTargetEye, MASK_SHOT, &filter, &trace);
							if (trace.fraction < 1.f)
								vPointDistances.pop_back();
						}
					}
				}

				if (bErase)
					iType = 0;
				else if (bNormal)
					iType &= ~PointType_Obscured;
				else
					iType &= ~PointType_Regular;
			}
		}

		if (!iType)
			it = vSpherePoints.erase(it);
		else
			++it;
	}

	std::sort(vPointDistances.begin(), vPointDistances.end(), [&](const auto& a, const auto& b) -> bool
		{
			return a.second < b.second;
		});
	std::vector<Point_t> vPoints = {};
	for (int i = 0; i < std::min(tInfo.m_iSplashCount, int(vPointDistances.size())); i++)
		vPoints.push_back(vPointDistances[i].first);

	const Vec3 vOriginal = target.m_pEntity->GetAbsOrigin();
	target.m_pEntity->SetAbsOrigin(target.m_vPos);
	for (auto it = vPoints.begin(); it != vPoints.end();)
	{
		auto& vPoint = *it;
		bool bValid = vPoint.m_Solution.m_iCalculated;
		if (bValid)
		{
			Vec3 vPos = {}; reinterpret_cast<CCollisionProperty*>(target.m_pEntity->GetCollideable())->CalcNearestPoint(vPoint.m_vPoint, &vPos);
			bValid = vPoint.m_vPoint.DistTo(vPos) < tInfo.m_flRadius;
		}

		if (bValid)
			++it;
		else
			it = vPoints.erase(it);
	}
	target.m_pEntity->SetAbsOrigin(vOriginal);

	return vPoints;
}

static inline float AABBLine(Vec3 vMins, Vec3 vMaxs, Vec3 vStart, Vec3 vDir)
{
	Vec3 a = {
		(vMins.x - vStart.x) / vDir.x,
		(vMins.y - vStart.y) / vDir.y,
		(vMins.z - vStart.z) / vDir.z
	};
	Vec3 b = {
		(vMaxs.x - vStart.x) / vDir.x,
		(vMaxs.y - vStart.y) / vDir.y,
		(vMaxs.z - vStart.z) / vDir.z
	};
	Vec3 c = {
		std::min(a.x, b.x),
		std::min(a.y, b.y),
		std::min(a.z, b.z)
	};
	return std::max(std::max(c.x, c.y), c.z);
}
static inline Vec3 PullPoint(Vec3 vPoint, Vec3 vLocalPos, Info_t& tInfo, Vec3 vMins, Vec3 vMaxs, Vec3 vTargetPos)
{
	auto HeightenLocalPos = [&]()
		{	// basic trajectory pass
			const float flGrav = tInfo.m_flGravity * 800.f;
			if (!flGrav)
				return vPoint;

			const Vec3 vDelta = vTargetPos - vLocalPos;
			const float flDist = vDelta.Length2D();

			const float flRoot = pow(tInfo.m_flVelocity, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(tInfo.m_flVelocity, 2));
			if (flRoot < 0.f)
				return vPoint;
			float flPitch = atan((pow(tInfo.m_flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));

			float flTime = flDist / (cos(flPitch) * tInfo.m_flVelocity) - tInfo.m_flOffsetTime;
			return vLocalPos + Vec3(0, 0, (flGrav * pow(flTime, 2)) / 2);
		};

	vLocalPos = HeightenLocalPos();
	Vec3 vForward, vRight, vUp; Math::AngleVectors(Math::CalcAngle(vLocalPos, vPoint), &vForward, &vRight, &vUp);
	Vec3 vStart = vLocalPos + (vForward * tInfo.m_vOffset.x) + (vRight * tInfo.m_vOffset.y) + (vUp * tInfo.m_vOffset.z);
	return vStart + (vPoint - vStart) * fabsf(AABBLine(vMins + vTargetPos, vMaxs + vTargetPos, vStart, vPoint - vStart));
}



static inline void SolveProjectileSpeed(CTFWeaponBase* pWeapon, const Vec3& vLocalPos, const Vec3& vTargetPos, float& flVelocity, float& flDragTime, const float flGravity)
{
	if (F::ProjSim.obj->m_dragBasis.IsZero())
		return;

	const float flGrav = flGravity * 800.0f;
	const Vec3 vDelta = vTargetPos - vLocalPos;
	const float flDist = vDelta.Length2D();

	const float flRoot = pow(flVelocity, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(flVelocity, 2));
	if (flRoot < 0.f)
		return;

	const float flPitch = atan((pow(flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
	const float flTime = flDist / (cos(flPitch) * flVelocity);

	float flDrag = 0.f;
	if (Vars::Aimbot::Projectile::DragOverride.Value)
		flDrag = Vars::Aimbot::Projectile::DragOverride.Value;
	else
	{
		switch (pWeapon->m_iItemDefinitionIndex()) // the remaps are dumb but they work so /shrug
		{
		case Demoman_m_GrenadeLauncher:
		case Demoman_m_GrenadeLauncherR:
		case Demoman_m_FestiveGrenadeLauncher:
		case Demoman_m_Autumn:
		case Demoman_m_MacabreWeb:
		case Demoman_m_Rainbow:
		case Demoman_m_SweetDreams:
		case Demoman_m_CoffinNail:
		case Demoman_m_TopShelf:
		case Demoman_m_Warhawk:
		case Demoman_m_ButcherBird:
		case Demoman_m_TheIronBomber: flDrag = Math::RemapValClamped(flVelocity, 1217.f, k_flMaxVelocity, 0.120f, 0.200f); break; // 0.120 normal, 0.200 capped, 0.300 v3000
		case Demoman_m_TheLochnLoad: flDrag = Math::RemapValClamped(flVelocity, 1504.f, k_flMaxVelocity, 0.070f, 0.085f); break; // 0.070 normal, 0.085 capped, 0.120 v3000
		case Demoman_m_TheLooseCannon: flDrag = Math::RemapValClamped(flVelocity, 1454.f, k_flMaxVelocity, 0.385f, 0.530f); break; // 0.385 normal, 0.530 capped, 0.790 v3000
		case Demoman_s_StickybombLauncher:
		case Demoman_s_StickybombLauncherR:
		case Demoman_s_FestiveStickybombLauncher:
		case Demoman_s_TheQuickiebombLauncher:
		case Demoman_s_TheScottishResistance: flDrag = Math::RemapValClamped(flVelocity, 922.f, k_flMaxVelocity, 0.085f, 0.190f); break; // 0.085 low, 0.190 capped, 0.230 v2400
		}
	}

	flDragTime = powf(flTime, 2) * flDrag / 1.5f; // rough estimate to prevent m_flTime being too low
	flVelocity = flVelocity - flVelocity * flTime * flDrag;

	if (Vars::Aimbot::Projectile::TimeOverride.Value)
		flDragTime = Vars::Aimbot::Projectile::TimeOverride.Value;
}
void CAimbotProjectile::CalculateAngle(const Vec3& vLocalPos, const Vec3& vTargetPos, Info_t& tInfo, int iSimTime, Solution_t& out, bool bAccuracy)
{
	if (out.m_iCalculated)
		return;

	const float flGrav = tInfo.m_flGravity * 800.f;

	float flPitch, flYaw;
	{	// basic trajectory pass
		const Vec3 vDelta = vTargetPos - vLocalPos;
		const float flDist = vDelta.Length2D();

		if (!flGrav)
		{
			const Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vTargetPos);
			flPitch = -DEG2RAD(vAngleTo.x);
			flYaw = DEG2RAD(vAngleTo.y);
		}
		else
		{	// arch
			const float flRoot = pow(tInfo.m_flVelocity, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(tInfo.m_flVelocity, 2));
			if (flRoot < 0.f)
			{
				out.m_iCalculated = 3; return;
			}
			flPitch = atan((pow(tInfo.m_flVelocity, 2) - sqrt(flRoot)) / (flGrav * flDist));
			flYaw = atan2(vDelta.y, vDelta.x);
		}
		out.m_flTime = flDist / (cos(flPitch) * tInfo.m_flVelocity) - tInfo.m_flOffsetTime;
		out.m_flPitch = flPitch = -RAD2DEG(flPitch) - tInfo.m_flUpFix, out.m_flYaw = flYaw = RAD2DEG(flYaw);
	}

	int iTimeTo = int(out.m_flTime / TICK_INTERVAL) + 1;
	if (iTimeTo > iSimTime ? 2 : 0)
	{
		out.m_iCalculated = 2; return;
	}

	ProjectileInfo projInfo = {};
	if (!F::ProjSim.GetInfo(tInfo.m_pLocal, tInfo.m_pWeapon, { flPitch, flYaw, 0 }, projInfo, bAccuracy ? ProjSim_Trace : ProjSim_None))
	{
		out.m_iCalculated = 3; return;
	}

	{	// correct angles
		Vec3 vAngle; Math::VectorAngles(vTargetPos - projInfo.m_vPos, vAngle);
		flPitch -= projInfo.m_vAng.x;
		out.m_flYaw = flYaw + vAngle.y - projInfo.m_vAng.y;
	}

	{	// calculate trajectory from projectile origin
		float flNewVel = tInfo.m_flVelocity, flDragTime = 0.f;
		SolveProjectileSpeed(tInfo.m_pWeapon, projInfo.m_vPos, vTargetPos, flNewVel, flDragTime, tInfo.m_flGravity);

		const Vec3 vDelta = vTargetPos - projInfo.m_vPos;
		const float flDist = vDelta.Length2D();

		if (!flGrav)
		{
			const Vec3 vAngleTo = Math::CalcAngle(projInfo.m_vPos, vTargetPos);
			out.m_flPitch = -DEG2RAD(vAngleTo.x);
		}
		else
		{	// arch
			const float flRoot = pow(flNewVel, 4) - flGrav * (flGrav * pow(flDist, 2) + 2.f * vDelta.z * pow(flNewVel, 2));
			if (flRoot < 0.f)
			{
				out.m_iCalculated = 3; return;
			}
			out.m_flPitch = atan((pow(flNewVel, 2) - sqrt(flRoot)) / (flGrav * flDist));
		}
		out.m_flTime = flDist / (cos(out.m_flPitch) * flNewVel) + flDragTime;
		out.m_flPitch = -RAD2DEG(out.m_flPitch) + flPitch - tInfo.m_flUpFix;
	}

	iTimeTo = int(out.m_flTime / TICK_INTERVAL) + 1;
	out.m_iCalculated = iTimeTo > iSimTime ? 2 : 1;
}

bool CAimbotProjectile::TestAngle(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, Target_t& target, Vec3& vPoint, Vec3& vAngles, int iSimTime, bool bSplash, std::deque<Vec3>* pProjectilePath)
{
	ProjectileInfo projInfo = {};
	if (!F::ProjSim.GetInfo(pLocal, pWeapon, vAngles, projInfo) || !F::ProjSim.Initialize(projInfo))
		return false;

	bool bDidHit = false;

	CGameTrace trace = {};
	CTraceFilterProjectile filter = {}; filter.pSkip = pLocal;
	CTraceFilterWorldAndPropsOnly filterWorld = {};

	if (!projInfo.m_flGravity)
	{
		SDK::TraceHull(projInfo.m_vPos, vPoint, projInfo.m_vHull * -1, projInfo.m_vHull, MASK_SOLID, &filterWorld, &trace);
		if (trace.fraction < 0.999f)
			return false;
	}

	if (Vars::Aimbot::General::AimType.Value != Vars::Aimbot::General::AimTypeEnum::Smooth)
		projInfo.m_vHull += Vec3(Vars::Aimbot::Projectile::HullIncrease.Value, Vars::Aimbot::Projectile::HullIncrease.Value, Vars::Aimbot::Projectile::HullIncrease.Value);

	const Vec3 vOriginal = target.m_pEntity->GetAbsOrigin();
	target.m_pEntity->SetAbsOrigin(target.m_vPos);
	for (int n = 1; n <= iSimTime; n++)
	{
		Vec3 vOld = F::ProjSim.GetOrigin();
		F::ProjSim.RunTick(projInfo);
		Vec3 vNew = F::ProjSim.GetOrigin();

		if (bDidHit)
		{
			trace.endpos = vNew;
			continue;
		}

		if (!bSplash)
			SDK::TraceHull(vOld, vNew, projInfo.m_vHull * -1, projInfo.m_vHull, MASK_SOLID, &filter, &trace);
		else
			SDK::TraceHull(vOld, vNew, projInfo.m_vHull * -1, projInfo.m_vHull, MASK_SOLID, &filterWorld, &trace);
		if (trace.DidHit())
		{
			const bool bTarget = trace.m_pEnt == target.m_pEntity;
			const bool bTime = iSimTime - n < 5;

			bool bValid = (bTarget || bSplash) && bTime;
			bValid = bValid && (bSplash ? SDK::VisPosWorld(nullptr, target.m_pEntity, trace.endpos, vPoint, MASK_SOLID) : true);

			if (bValid)
			{
				if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth)
				{
					if (bSplash && trace.endpos.DistTo(vPoint) > projInfo.m_flVelocity * TICK_INTERVAL)
						break;

					// attempted to have a headshot check though this seems more detrimental than useful outside of smooth aimbot
					if (target.m_nAimedHitbox == HITBOX_HEAD)
					{	// i think this is accurate? nope, 220
						const Vec3 vOffset = (trace.endpos - vNew) + (vOriginal - target.m_vPos);

						Vec3 vOld = F::ProjSim.GetOrigin() + vOffset;
						F::ProjSim.RunTick(projInfo);
						Vec3 vNew = F::ProjSim.GetOrigin() + vOffset;

						CGameTrace trace = {};
						SDK::Trace(vOld, vNew, MASK_SHOT, &filter, &trace);
						trace.endpos -= vOffset;

						if (trace.DidHit() && (trace.m_pEnt != target.m_pEntity || trace.hitbox != HITBOX_HEAD))
							break;

						if (!trace.DidHit()) // loop and see if closest hitbox is head
						{
							auto pModel = target.m_pEntity->GetModel();
							if (!pModel) break;
							auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
							if (!pHDR) break;
							auto pSet = pHDR->pHitboxSet(target.m_pEntity->As<CTFPlayer>()->m_nHitboxSet());
							if (!pSet) break;

							matrix3x4* aBones = H::Entities.GetBones(target.m_pEntity->entindex());
							if (!aBones)
								break;

							Vec3 vForward = vOld - vNew; vForward.Normalize();
							const Vec3 vPos = trace.endpos + vForward * 16 + vOriginal - target.m_vPos;

							//G::LineStorage.clear();
							//G::LineStorage.push_back({ { pLocal->GetShootPos(), vPos }, I::GlobalVars->curtime + 5.f, Vars::Colors::Prediction.Value });

							float closestDist = 0.f; int closestId = -1;
							for (int i = 0; i < pSet->numhitboxes; ++i)
							{
								auto pBox = pSet->pHitbox(i);
								if (!pBox)
									continue;

								Vec3 vCenter; Math::VectorTransform((pBox->bbmin + pBox->bbmax) / 2, aBones[pBox->bone], vCenter);

								const float flDist = vPos.DistTo(vCenter);
								if (closestId != -1 && flDist < closestDist || closestId == -1)
								{
									closestDist = flDist;
									closestId = i;
								}
							}

							if (closestId != 0)
								break;
							bDidHit = true;
						}
					}
				}

				bDidHit = true;
			}
			else if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER && !bSplash && bTarget)
			{	// run for 5 more ticks to check for splash
				iSimTime = n + 5;
				bSplash = true;
			}
			else
				break;

			if (!bSplash)
				trace.endpos = vNew;

			if (!bTarget)
				break;
		}
	}
	target.m_pEntity->SetAbsOrigin(vOriginal);

	if (bDidHit && pProjectilePath)
	{
		projInfo.m_vPath.push_back(trace.endpos);
		*pProjectilePath = projInfo.m_vPath;
	}

	return bDidHit;
}



int CAimbotProjectile::CanHit(Target_t& target, CTFPlayer* pLocal, CTFWeaponBase* pWeapon,
	std::deque<Vec3>* pPlayerPath, std::deque<Vec3>* pProjectilePath, std::vector<DrawBox>* pBoxes, float* pTimeTo)
{
	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(target.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
		return false;

	PlayerStorage storage;
	F::MoveSim.Initialize(target.m_pEntity, storage);
	target.m_vPos = target.m_pEntity->m_vecOrigin();
	const float flSize = target.m_pEntity->m_vecMins().DistTo(target.m_pEntity->m_vecMaxs());

	int iMaxTime, iSplash; Info_t tInfo = { pLocal, pWeapon };
	ProjectileInfo projInfo = {};
	{
		if (!F::ProjSim.GetInfo(pLocal, pWeapon, {}, projInfo, ProjSim_None) || !F::ProjSim.Initialize(projInfo, false))
		{
			F::MoveSim.Restore(storage);
			return false;
		}

		tInfo.m_vLocalEye = pLocal->GetShootPos();
		tInfo.m_vTargetEye = target.m_pEntity->As<CTFPlayer>()->GetViewOffset();

		tInfo.m_flLatency = F::Backtrack.GetReal() + TICKS_TO_TIME(G::AnticipatedChoke + Vars::Aimbot::Projectile::LatencyOffset.Value);
		tInfo.m_iLatency = TIME_TO_TICKS(tInfo.m_flLatency);

		iMaxTime = TIME_TO_TICKS(std::min(projInfo.m_flLifetime, Vars::Aimbot::Projectile::PredictionTime.Value));

		Vec3 vVelocity = F::ProjSim.GetVelocity();
		tInfo.m_flVelocity = vVelocity.Length(); // account for up velocity & capped velocity
		Vec3 vBadAngle = {}; Math::VectorAngles(vVelocity, vBadAngle); tInfo.m_flUpFix = vBadAngle.x; // account for up velocity

		tInfo.m_vOffset = projInfo.m_vPos - tInfo.m_vLocalEye; tInfo.m_vOffset.y *= -1;
		tInfo.m_flOffsetTime = tInfo.m_vOffset.Length() / tInfo.m_flVelocity; // silly

		tInfo.m_flGravity = projInfo.m_flGravity;
		tInfo.m_flRadius = GetSplashRadius(pWeapon, pLocal); tInfo.m_flRadiusTime = tInfo.m_flRadius / tInfo.m_flVelocity;
		tInfo.m_flBoundingRadius = (tInfo.m_flRadius + flSize) / tInfo.m_flVelocity;

		iSplash = Vars::Aimbot::Projectile::SplashPrediction.Value && tInfo.m_flRadius ? Vars::Aimbot::Projectile::SplashPrediction.Value : Vars::Aimbot::Projectile::SplashPredictionEnum::Off;
		tInfo.m_iSplashCount = !projInfo.m_flGravity ? Vars::Aimbot::Projectile::SplashCountDirect.Value : Vars::Aimbot::Projectile::SplashCountArc.Value;
		if (Vars::Aimbot::Projectile::SplashRocket.Value)
		{
			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_ROCKETLAUNCHER:
			case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			case TF_WEAPON_PARTICLE_CANNON:
				tInfo.m_bRocket = true;
			}
		}

		tInfo.m_iPrimeTime = TIME_TO_TICKS(PrimeTime(pWeapon));
	}

	int iReturn = false;

	auto mDirectPoints = iSplash == Vars::Aimbot::Projectile::SplashPredictionEnum::Only ? std::unordered_map<int, Vec3>() : GetDirectPoints(target, tInfo);
	auto vSpherePoints = !iSplash ? std::vector<std::pair<Vec3, int>>() : ComputeSphere(tInfo.m_flRadius + flSize, Vars::Aimbot::Projectile::SplashPoints.Value, 1.5f);
#ifdef SPLASH_DEBUG2
	for (auto& [vPoint, _] : vSpherePoints)
		G::BoxStorage.push_back({ target.m_pEntity->m_vecOrigin() + tInfo.m_vTargetEye + vPoint * tInfo.m_flRadius / (tInfo.m_flRadius + flSize), { -1, -1, -1 }, { 1, 1, 1 }, {}, I::GlobalVars->curtime + 60.f, Vars::Colors::Local.Value });
#endif
	
	Vec3 vAngleTo, vPredicted, vTarget;
	int iLowestPriority = std::numeric_limits<int>::max(); float flLowestDist = std::numeric_limits<float>::max();
	int iLowestSmoothPriority = iLowestPriority; float flLowestSmoothDist = flLowestDist;
	for (int i = 1; i < iMaxTime; i++)
	{
		const int iSimTime = i - tInfo.m_iLatency;
		if (!storage.m_bFailed)
		{
			F::MoveSim.RunTick(storage);
			target.m_vPos = storage.m_vPredictedOrigin;
		}

		std::vector<Point_t> vSplashPoints = {};
		if (iSplash)
		{
			Solution_t solution; CalculateAngle(tInfo.m_vLocalEye, target.m_vPos, tInfo, iSimTime, solution, false);
			if (solution.m_iCalculated != 3)
			{
				const float flTimeTo = solution.m_flTime - TICKS_TO_TIME(iSimTime);
				if (flTimeTo < -tInfo.m_flBoundingRadius || vSpherePoints.empty())
					break;
				else if (abs(flTimeTo) < tInfo.m_flBoundingRadius)
					vSplashPoints = GetSplashPoints(target, vSpherePoints, tInfo, iSimTime);
			}
		}
		else if (mDirectPoints.empty())
			break;

		std::vector<std::tuple<Point_t, int, int>> vPoints = {};
		for (auto& [iIndex, vPoint] : mDirectPoints)
			vPoints.push_back({ { target.m_vPos + vPoint, {}}, iIndex + (iSplash == Vars::Aimbot::Projectile::SplashPredictionEnum::Prefer ? tInfo.m_iSplashCount : 0), iIndex });
		for (auto& vPoint : vSplashPoints)
			vPoints.push_back({ vPoint, iSplash == Vars::Aimbot::Projectile::SplashPredictionEnum::Include ? 3 : 0, -1 });

		for (auto& [vPoint, iPriority, iIndex] : vPoints) // get most ideal point
		{
			const bool bSplash = iIndex == -1;
			Vec3 vOriginalPoint = vPoint.m_vPoint;

			if (target.m_nAimedHitbox == HITBOX_HEAD)
				vPoint.m_vPoint = PullPoint(vPoint.m_vPoint, tInfo.m_vLocalEye, tInfo, target.m_pEntity->m_vecMins(), target.m_pEntity->m_vecMaxs(), target.m_vPos);

			float flDist = bSplash ? target.m_vPos.DistTo(vPoint.m_vPoint) : flLowestDist;
			bool bPriority = bSplash ? iPriority <= iLowestPriority : iPriority < iLowestPriority;
			bool bTime = bSplash || tInfo.m_iPrimeTime < iSimTime || storage.m_MoveData.m_vecVelocity.IsZero();
			bool bDist = !bSplash || flDist < flLowestDist;
			if (!bSplash && !bPriority)
				mDirectPoints.erase(iIndex);
			if (!bPriority || !bTime || !bDist)
				continue;

			CalculateAngle(tInfo.m_vLocalEye, vPoint.m_vPoint, tInfo, iSimTime, vPoint.m_Solution);
			if (!bSplash && (vPoint.m_Solution.m_iCalculated == 1 || vPoint.m_Solution.m_iCalculated == 3))
				mDirectPoints.erase(iIndex);
			if (vPoint.m_Solution.m_iCalculated != 1)
				continue;

			Vec3 vAngles = Aim(G::CurrentUserCmd->viewangles, { vPoint.m_Solution.m_flPitch, vPoint.m_Solution.m_flYaw, 0.f });
			std::deque<Vec3> vProjLines;
			if (TestAngle(pLocal, pWeapon, target, vPoint.m_vPoint, vAngles, iSimTime, bSplash, &vProjLines))
			{
				iLowestPriority = iPriority; flLowestDist = flDist;
				vAngleTo = vAngles, vPredicted = target.m_vPos, vTarget = vOriginalPoint;
				*pTimeTo = vPoint.m_Solution.m_flTime + tInfo.m_flLatency;
				*pPlayerPath = storage.m_vPath;
				if (!pPlayerPath->empty())
					pPlayerPath->push_back(storage.m_MoveData.m_vecAbsOrigin);
				*pProjectilePath = vProjLines;
			}
			else if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
				&& Vars::Aimbot::General::Smoothing.Value != 0.f
				&& Vars::Aimbot::General::Smoothing.Value != 100.f)
			{
				bPriority = bSplash ? iPriority <= iLowestSmoothPriority : iPriority < flLowestSmoothDist;
				bDist = !bSplash || flDist < flLowestDist;
				if (!bPriority || !bDist)
					continue;

				Vec3 vPlainAngles = Aim(G::CurrentUserCmd->viewangles, { vPoint.m_Solution.m_flPitch, vPoint.m_Solution.m_flYaw, 0.f }, Vars::Aimbot::General::AimTypeEnum::Plain);
				if (TestAngle(pLocal, pWeapon, target, vPoint.m_vPoint, vPlainAngles, iSimTime, bSplash))
				{
					iLowestSmoothPriority = iPriority; flLowestSmoothDist = flDist;
					target.m_vAngleTo = vAngles;
					iReturn = 2;
				}
			}
		}
	}
	F::MoveSim.Restore(storage);

	const float flTime = TICKS_TO_TIME(pProjectilePath->size());
	target.m_vPos = vTarget;

	if (iLowestPriority != std::numeric_limits<int>::max() &&
		(target.m_TargetType == ETargetType::Player ? !storage.m_bFailed : true)) // don't attempt to aim at players when movesim fails
	{
		target.m_vAngleTo = vAngleTo;
		if (Vars::Visuals::Hitbox::Enabled.Value & Vars::Visuals::Hitbox::EnabledEnum::OnShot)
		{
			if (!Vars::Colors::BoundHitboxEdge.Value.a && !Vars::Colors::BoundHitboxFace.Value.a)
				return true;

			pBoxes->push_back({ vPredicted, target.m_pEntity->m_vecMins(), target.m_pEntity->m_vecMaxs(), Vec3(), I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flTime : 5.f), Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true });

			const float flSize = std::clamp(projInfo.m_vHull.x, 1.f, 3.f);
			const Vec3 vSize = { flSize, flSize, flSize };
			pBoxes->push_back({ vTarget, vSize * -1, vSize, Vec3(), I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flTime : 5.f), Vars::Colors::BoundHitboxEdge.Value, Vars::Colors::BoundHitboxFace.Value, true });

			if (Vars::Debug::Info.Value && target.m_nAimedHitbox == HITBOX_HEAD) // huntsman head
			{
				const Vec3 vOriginOffset = target.m_pEntity->m_vecOrigin() - vPredicted;

				matrix3x4* aBones = H::Entities.GetBones(target.m_pEntity->entindex());
				if (!aBones)
					return true;

				auto vBoxes = F::Visuals.GetHitboxes(aBones, target.m_pEntity->As<CTFPlayer>(), { HITBOX_HEAD });
				for (auto& bBox : vBoxes)
				{
					bBox.m_vecPos -= vOriginOffset;
					bBox.m_flTime = I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flTime : 5.f);
					pBoxes->push_back(bBox);
				}
			}
			if (Vars::Debug::Info.Value && target.m_nAimedHitbox == HITBOX_HEAD) // huntsman head, broken; removeme once 220 is fixed
			{
				const Vec3 vOriginOffset = target.m_pEntity->m_vecOrigin() - vPredicted;

				matrix3x4* aBones = H::Entities.GetBones(target.m_pEntity->entindex());
				if (!aBones)
					return true;

				auto vBoxes = F::Visuals.GetHitboxes(aBones, target.m_pEntity->As<CTFPlayer>(), { HITBOX_HEAD });
				for (auto& bBox : vBoxes)
				{
					bBox.m_vecPos -= vOriginOffset;
					bBox.m_flTime = I::GlobalVars->curtime + (Vars::Visuals::Simulation::Timed.Value ? flTime : 5.f);
					bBox.m_vecOrientation = Vec3();
					pBoxes->push_back(bBox);
				}
			}
		}
		return true;
	}

	return iReturn;
}



Vec3 CAimbotProjectile::Aim(Vec3 vCurAngle, Vec3 vToAngle, int iMethod)
{
	Vec3 vReturn = {};

	Math::ClampAngles(vToAngle);

	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		vReturn = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	{
		auto shortDist = [](const float flAngleA, const float flAngleB)
			{
				const float flDelta = fmodf((flAngleA - flAngleB), 360.f);
				return fmodf(2 * flDelta, 360.f) - flDelta;
			};
		const float t = 1.f - Vars::Aimbot::General::Smoothing.Value / 100.f;
		vReturn.x = vCurAngle.x - shortDist(vCurAngle.x, vToAngle.x) * t;
		vReturn.y = vCurAngle.y - shortDist(vCurAngle.y, vToAngle.y) * t;
		break;
	}
	}

	return vReturn;
}

// assume angle calculated outside with other overload
void CAimbotProjectile::Aim(CUserCmd* pCmd, Vec3& vAngle)
{
	bool bDoubleTap = G::DoubleTap || F::Ticks.GetTicks(H::Entities.GetWeapon());
	if (Vars::Aimbot::General::AimType.Value != Vars::Aimbot::General::AimTypeEnum::Silent)
	{
		//pCmd->viewangles = vAngle; // retarded, overshooting with this uncommented
		I::EngineClient->SetViewAngles(vAngle);
	}
	else if (G::Attacking == 1 || bDoubleTap)
	{
		SDK::FixMovement(pCmd, vAngle);
		pCmd->viewangles = vAngle;
		G::PSilentAngles = true;
	}
}

bool CAimbotProjectile::RunMain(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const int nWeaponID = pWeapon->GetWeaponID();

	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = iStaticAimType = Vars::Aimbot::General::AimType.Value;

	switch (nWeaponID)
	{
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_PIPEBOMBLAUNCHER:
	case TF_WEAPON_CANNON:
		if (!Vars::Aimbot::General::AutoShoot.Value && !iRealAimType && iLastAimType && G::Attacking)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
	}

	static int iAimType = 0;
	if (!G::Throwing)
		iAimType = Vars::Aimbot::General::AimType.Value;
	else if (iAimType)
		Vars::Aimbot::General::AimType.Value = iAimType;

	if (Vars::Aimbot::General::AimHoldsFire.Value == Vars::Aimbot::General::AimHoldsFireEnum::Always && !G::CanPrimaryAttack && G::LastUserCmd->buttons & IN_ATTACK && Vars::Aimbot::General::AimType.Value && !pWeapon->IsInReload())
		pCmd->buttons |= IN_ATTACK;
	// the G::DoubleTap condition is not a great fix here and actually properly predicting when shots will be fired should likely be done over this, but it's fine for now
	if (!Vars::Aimbot::General::AimType.Value || !G::CanPrimaryAttack && !G::Reloading && !G::DoubleTap && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent && nWeaponID != TF_WEAPON_PIPEBOMBLAUNCHER && nWeaponID != TF_WEAPON_CANNON)
		return false;

	auto targets = SortTargets(pLocal, pWeapon);
	if (targets.empty())
		return false;

	if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon && iRealAimType
		&& (nWeaponID == TF_WEAPON_COMPOUND_BOW || nWeaponID == TF_WEAPON_PIPEBOMBLAUNCHER || nWeaponID == TF_WEAPON_CANNON))
	{
		pCmd->buttons |= IN_ATTACK;
		if (!G::CanPrimaryAttack && !G::Reloading && Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
			return false;
	}

#ifdef SPLASH_DEBUG1
	G::LineStorage.clear();
#endif
#ifdef SPLASH_DEBUG2
	G::BoxStorage.clear();
#endif
	for (auto& target : targets)
	{
		float flTimeTo = 0.f; std::deque<Vec3> vPlayerPath, vProjectilePath; std::vector<DrawBox> vBoxes = {};
		const int iResult = CanHit(target, pLocal, pWeapon, &vPlayerPath, &vProjectilePath, &vBoxes, &flTimeTo);
		if (!iResult) continue;
		if (iResult == 2)
		{
			Aim(pCmd, target.m_vAngleTo);
			break;
		}

		G::Target = { target.m_pEntity->entindex(), I::GlobalVars->tickcount };
		if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
			G::AimPosition = target.m_vPos;

		if (Vars::Aimbot::General::AutoShoot.Value)
		{
			pCmd->buttons |= IN_ATTACK;

			if (pWeapon->m_iItemDefinitionIndex() == Soldier_m_TheBeggarsBazooka)
			{
				if (pWeapon->m_iClip1() > 0)
					pCmd->buttons &= ~IN_ATTACK;
			}
			else
			{
				switch (nWeaponID)
				{
				case TF_WEAPON_COMPOUND_BOW:
				case TF_WEAPON_PIPEBOMBLAUNCHER:
					if (pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f)
						pCmd->buttons &= ~IN_ATTACK;
					break;
				case TF_WEAPON_CANNON:
					if (pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f)
					{
						bool bHealth = target.m_pEntity->IsPlayer() && target.m_pEntity->As<CTFPlayer>()->m_iHealth() > 50 || target.m_pEntity->IsBuilding() && target.m_pEntity->As<CBaseObject>()->m_iHealth() > 50;
						if (Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::ChargeWeapon && bHealth)
						{
							float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() - I::GlobalVars->curtime;
							if (std::clamp(flCharge - 0.05f, 0.f, 1.f) < flTimeTo)
								pCmd->buttons &= ~IN_ATTACK;
						}
						else
							pCmd->buttons &= ~IN_ATTACK;
					}
					break;
				case TF_WEAPON_BAT_WOOD:
				case TF_WEAPON_BAT_GIFTWRAP:
					pCmd->buttons &= ~IN_ATTACK, pCmd->buttons |= IN_ATTACK2;
				}
			}
		}

		F::Aimbot.bRan = G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);

		if (G::Attacking == 1 || !Vars::Aimbot::General::AutoShoot.Value)
		{
			if (Vars::Visuals::Simulation::PlayerPath.Value
				|| Vars::Visuals::Simulation::ProjectilePath.Value && G::Attacking == 1
				|| Vars::Visuals::Hitbox::Enabled.Value & Vars::Visuals::Hitbox::EnabledEnum::OnShot)
			{
				G::PathStorage.clear();
				G::LineStorage.clear();
			}

			if (Vars::Visuals::Simulation::PlayerPath.Value)
			{
				if (Vars::Colors::PlayerPath.Value.a)
					G::PathStorage.push_back({ vPlayerPath, Vars::Visuals::Simulation::Timed.Value ? -int(vPlayerPath.size()) : I::GlobalVars->curtime + 5.f, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value });
				if (Vars::Colors::PlayerPathClipped.Value.a)
					G::PathStorage.push_back({ vPlayerPath, Vars::Visuals::Simulation::Timed.Value ? -int(vPlayerPath.size()) : I::GlobalVars->curtime + 5.f, Vars::Colors::PlayerPathClipped.Value, Vars::Visuals::Simulation::PlayerPath.Value, true });
			}
			if (Vars::Visuals::Simulation::ProjectilePath.Value && G::Attacking == 1)
			{
				if (Vars::Colors::ProjectilePath.Value.a)
					G::PathStorage.push_back({ vProjectilePath, Vars::Visuals::Simulation::Timed.Value ? -int(vProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + 5.f, Vars::Colors::ProjectilePath.Value, Vars::Visuals::Simulation::ProjectilePath.Value });
				if (Vars::Colors::ProjectilePathClipped.Value.a)
					G::PathStorage.push_back({ vProjectilePath, Vars::Visuals::Simulation::Timed.Value ? -int(vProjectilePath.size()) - TIME_TO_TICKS(F::Backtrack.GetReal()) : I::GlobalVars->curtime + 5.f, Vars::Colors::ProjectilePathClipped.Value, Vars::Visuals::Simulation::ProjectilePath.Value, true });
			}
			if (Vars::Visuals::Hitbox::Enabled.Value & Vars::Visuals::Hitbox::EnabledEnum::OnShot)
			{
				G::BoxStorage.clear();
				G::BoxStorage.insert(G::BoxStorage.end(), vBoxes.begin(), vBoxes.end());
			}
		}

		Aim(pCmd, target.m_vAngleTo);
		if (G::PSilentAngles)
		{
			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_FLAMETHROWER: // angles show up anyways
			case TF_WEAPON_CLEAVER: // can't psilent with these weapons, they use SetContextThink
			case TF_WEAPON_JAR:
			case TF_WEAPON_JAR_MILK:
			case TF_WEAPON_JAR_GAS:
			case TF_WEAPON_BAT_WOOD:
			case TF_WEAPON_BAT_GIFTWRAP:
				G::PSilentAngles = false, G::SilentAngles = true;
			}
		}
		return true;
	}

	return false;
}

void CAimbotProjectile::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	const bool bSuccess = RunMain(pLocal, pWeapon, pCmd);

	float flAmount = 0.f;
	if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER)
	{
		const float flCharge = pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<CTFPipebombLauncher>()->m_flChargeBeginTime() : 0.f;
		flAmount = Math::RemapValClamped(flCharge, 0.f, SDK::AttribHookValue(4.f, "stickybomb_charge_rate", pWeapon), 0.f, 1.f);
	}
	else if (pWeapon->GetWeaponID() == TF_WEAPON_CANNON)
	{
		const float flMortar = SDK::AttribHookValue(0.f, "grenade_launcher_mortar_mode", pWeapon);
		const float flCharge = pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() > 0.f ? I::GlobalVars->curtime - pWeapon->As<CTFGrenadeLauncher>()->m_flDetonateTime() : -flMortar;
		flAmount = flMortar ? Math::RemapValClamped(flCharge, -flMortar, 0.f, 0.f, 1.f) : 0.f;
	}

	if (pWeapon->GetWeaponID() == TF_WEAPON_PIPEBOMBLAUNCHER && Vars::Aimbot::Projectile::AutoRelease.Value && flAmount > Vars::Aimbot::Projectile::AutoRelease.Value / 100)
		pCmd->buttons &= ~IN_ATTACK;
	else if (G::CanPrimaryAttack && Vars::Aimbot::Projectile::Modifiers.Value & Vars::Aimbot::Projectile::ModifiersEnum::CancelCharge)
	{
		if (m_bLastTickHeld && (G::LastUserCmd->buttons & IN_ATTACK && !(pCmd->buttons & IN_ATTACK) && !bSuccess || flAmount > 0.95f))
		{
			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_COMPOUND_BOW:
				pCmd->buttons |= IN_ATTACK2;
				pCmd->buttons &= ~IN_ATTACK;
				break;
			case TF_WEAPON_CANNON:
				if (auto pSwap = pLocal->GetWeaponFromSlot(SLOT_SECONDARY))
				{
					pCmd->weaponselect = pSwap->entindex();
					m_bLastTickCancel = pWeapon->entindex();
				}
				break;
			case TF_WEAPON_PIPEBOMBLAUNCHER:
				if (auto pSwap = pLocal->GetWeaponFromSlot(SLOT_PRIMARY))
				{
					pCmd->weaponselect = pSwap->entindex();
					m_bLastTickCancel = pWeapon->entindex();
				}
			}
		}
	}

	m_bLastTickHeld = Vars::Aimbot::General::AimType.Value;
}