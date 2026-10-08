#include "Combat/KillStatsRules.h"

namespace
{
	// Attacker-source matching contract (exact, case-sensitive like Godot's ==): the English producer text and the
	// legacy Russian spelling both count, so producers (and operative DisplayNames set in Blueprint assets) can switch
	// language independently.
	bool IsCommanderName(const FString& Key)
	{
		return Key == TEXT("Commander") || Key == TEXT("Командир"); // cyrillic-ok: legacy Russian data
	}

	bool IsEngineerName(const FString& Key)
	{
		return Key == TEXT("Engineer") || Key == TEXT("Инженер"); // cyrillic-ok: legacy Russian data
	}

	bool IsMedicName(const FString& Key)
	{
		return Key == TEXT("Medic-Sapper") || Key == TEXT("Medic") || Key == TEXT("Медик-сапёр") || Key == TEXT("Медик"); // cyrillic-ok: legacy Russian data
	}

	bool IsTurretSource(const FString& Source)
	{
		return Source == TEXT("Turret") || Source == TEXT("Турель"); // cyrillic-ok: legacy Russian data
	}

	bool IsMineSource(const FString& Source)
	{
		return Source == TEXT("Mine") || Source == TEXT("Мина"); // cyrillic-ok: legacy Russian data
	}

	bool IsBarricadeSource(const FString& Source)
	{
		return Source == TEXT("Barricade") || Source == TEXT("Баррикада"); // cyrillic-ok: legacy Russian data
	}

	FMemberKillStats* FindMember(FSquadKillStats& Stats, const FString& Key)
	{
		if (IsCommanderName(Key))
		{
			return &Stats.Commander;
		}
		if (IsEngineerName(Key))
		{
			return &Stats.Engineer;
		}
		if (IsMedicName(Key))
		{
			return &Stats.Medic;
		}
		return nullptr;
	}
}

void KillStatsRules::RegisterKill(FSquadKillStats& Stats, EEnemyArchetype Type, const FString& Source, const FString& LeaderName)
{
	FMemberKillStats* Member = nullptr;
	const bool bTurret = IsTurretSource(Source);
	const bool bMine = IsMineSource(Source);
	if (bTurret || IsCommanderName(Source))
	{
		Member = &Stats.Commander;
	}
	else if (IsEngineerName(Source))
	{
		Member = &Stats.Engineer;
	}
	else if (bMine || IsMedicName(Source))
	{
		Member = &Stats.Medic;
	}
	else
	{
		Member = LeaderName.IsEmpty() ? &Stats.Commander : FindMember(Stats, LeaderName);
	}

	if (!Member)
	{
		return;
	}
	switch (Type)
	{
	case EEnemyArchetype::FrostHound: ++Member->Hound; break;
	case EEnemyArchetype::Spitter: ++Member->Spitter; break;
	case EEnemyArchetype::Brute: ++Member->Brute; break;
	default: break;
	}
	++Member->Total;
	if (bTurret)
	{
		// Godot: only the commander's entry has TURRET_KILLS (a turret always counts for him).
		++Member->TurretKills;
		++Stats.TurretKills;
	}
	if (bMine)
	{
		++Member->MineKills;
		++Stats.MineKills;
	}
	if (IsBarricadeSource(Source))
	{
		++Stats.BarricadeKills;
	}
}

FString KillStatsRules::Format(const FSquadKillStats& Stats)
{
	const FMemberKillStats& C = Stats.Commander;
	const FMemberKillStats& E = Stats.Engineer;
	const FMemberKillStats& M = Stats.Medic;
	FString Text = FString::Printf(TEXT("📊 FINAL COMBAT STATS (TOTAL KILLED: %d):\n"), Stats.GetTotal());
	Text += FString::Printf(TEXT("🎖️ Commander: 🐺 %d | 🏹 %d | ❄️ %d  (💥 Turret: %d) ➔ %d kills\n"), C.Hound, C.Spitter, C.Brute, C.TurretKills, C.Total);
	Text += FString::Printf(TEXT("🛠️ Engineer: 🐺 %d | 🏹 %d | ❄️ %d ➔ %d kills\n"), E.Hound, E.Spitter, E.Brute, E.Total);
	Text += FString::Printf(TEXT("🩺 Medic-Sapper: 🐺 %d | 🏹 %d | ❄️ %d  (💣 Mines: %d) ➔ %d kills"), M.Hound, M.Spitter, M.Brute, M.MineKills, M.Total);
	return Text;
}
