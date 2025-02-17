#pragma once
#include "../../../SDK/SDK.h"

enum ProjSim_
{
	ProjSim_None = 0,
	ProjSim_Trace = 1 << 0,
	ProjSim_InitCheck = 1 << 2,
	ProjSim_Quick = 1 << 1
};

struct ProjectileInfo
{
	ProjectileType_t m_iType{};

	Vec3 m_vPos = {};
	Vec3 m_vAng = {};
	Vec3 m_vHull = {};

	float m_flVelocity = 0.f;
	float m_flGravity = 0.f;
	bool m_bNoSpin = false;
	float m_flLifetime = 60.f;

	std::deque<Vec3> m_vPath = {};

	bool m_bQuick = false;
};

class CProjectileSimulation
{
	bool GetInfoMain(CTFPlayer* pPlayer, CTFWeaponBase* pWeapon, Vec3 vAngles, ProjectileInfo& out, bool bTrace, bool bQuick, float flAutoCharge);

	const objectparams_t g_PhysDefaultObjectParams =
	{
		NULL,
		1.0, //mass
		1.0, // inertia
		0.1f, // damping
		0.1f, // rotdamping
		0.05f, // rotIntertiaLimit
		"DEFAULT",
		NULL,// game data
		0.f, // volume (leave 0 if you don't have one or call physcollision->CollideVolume() to compute it)
		1.0f, // drag coefficient
		true,// enable collisions?
	};

public:
	bool GetInfo(CTFPlayer* pPlayer, CTFWeaponBase* pWeapon, Vec3 vAngles, ProjectileInfo& out, int iFlags = ProjSim_Trace | ProjSim_InitCheck, float flAutoCharge = -1.f);
	bool Initialize(ProjectileInfo& info, bool bSimulate = true);
	void RunTick(ProjectileInfo& info, bool bPath = true);
	Vec3 GetOrigin();
	Vec3 GetVelocity();

	IPhysicsEnvironment* env = nullptr;
	IPhysicsObject* obj = nullptr;
};

ADD_FEATURE(CProjectileSimulation, ProjSim)