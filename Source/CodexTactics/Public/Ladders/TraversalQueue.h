#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtrTemplates.h"

/**
 * One-at-a-time traversal queue: the ladder's lease + FIFO line (docs/design/LADDERS.md 7.1, 2026-10-23), shared since R17-1 (designer
 * 2026-11-17) by the squad's turn-taking climb onto / over a mantle obstacle (docs/design/MANTLING.md 6.4, USquadSubsystem's squad mantle).
 * One holder at a time; the others wait in the order they joined. A waiter that no longer waits HERE (killed, re-ordered - the owner's
 * IsWaiting says) is skipped, never blocks the line. The spot rule: the waiter with place Index stands SpotDistanceCm(Index) out from the
 * traversal's start (the ladder: out from the rungs below / back from the edge on top; the mantle: out from the obstacle's face).
 * UE-only (Godot had neither ladders nor mantling). Pure apart from the weak pointers: unit tested (CodexTactics.Ladder.TraversalQueue.*).
 */
template <typename T>
struct TTraversalQueue
{
	/** Does this one still wait for this traversal (alive, queued here)? */
	using FIsWaiting = TFunctionRef<bool(const T*)>;

	/** The waiter with place Index (0 = next) stands this far out from the traversal's start, cm. */
	static float SpotDistanceCm(int32 Index, float FirstCm, float SpacingCm)
	{
		return FirstCm + SpacingCm * static_cast<float>(FMath::Max(0, Index));
	}

	T* GetHolder() const { return Holder.Get(); }

	/** Someone else holds it (a destroyed holder holds nothing). */
	bool IsHeldByOther(const T* Who) const { return Holder.IsValid() && Holder.Get() != Who; }

	/** Takes it for Who: true when nobody else holds it and nobody before him still waits (he leaves the line). */
	bool TryLease(T* Who, FIsWaiting IsWaiting)
	{
		if (!Who || IsBusyFor(Who, IsWaiting))
		{
			return false;
		}
		Holder = Who;
		Leave(Who);
		return true;
	}

	/** Who lets it go (a stale holder is dropped too). */
	void Release(const T* Who)
	{
		if (!Holder.IsValid() || Holder.Get() == Who)
		{
			Holder.Reset();
		}
	}

	/** Who waits (added once, at the back of the line). */
	void Join(T* Who)
	{
		if (Who && !Waiting.Contains(Who))
		{
			Waiting.Add(Who);
		}
	}

	/** Who leaves the line (dead entries are swept out with him). */
	void Leave(const T* Who)
	{
		Waiting.RemoveAll([Who](const TWeakObjectPtr<T>& Entry) { return !Entry.IsValid() || Entry.Get() == Who; });
	}

	/** Who's place in the line (0 = next), INDEX_NONE when not waiting. Entries that no longer wait here do not count. */
	int32 GetIndex(const T* Who, FIsWaiting IsWaiting) const
	{
		int32 Index = 0;
		for (const TWeakObjectPtr<T>& Entry : Waiting)
		{
			const T* Waiter = Entry.Get();
			if (!Waiter || !IsWaiting(Waiter))
			{
				continue; // left without leaving the list (killed, re-ordered): skipped
			}
			if (Waiter == Who)
			{
				return Index;
			}
			++Index;
		}
		return INDEX_NONE;
	}

	/** Live waiters. */
	int32 Length(FIsWaiting IsWaiting) const
	{
		int32 Count = 0;
		for (const TWeakObjectPtr<T>& Entry : Waiting)
		{
			Count += Entry.IsValid() && IsWaiting(Entry.Get()) ? 1 : 0;
		}
		return Count;
	}

	/** Not Who's turn: someone else holds it, or someone came before him and still waits. */
	bool IsBusyFor(const T* Who, FIsWaiting IsWaiting) const
	{
		if (IsHeldByOther(Who))
		{
			return true;
		}
		for (const TWeakObjectPtr<T>& Entry : Waiting)
		{
			const T* Waiter = Entry.Get();
			if (Waiter && IsWaiting(Waiter))
			{
				return Waiter != Who; // the first live waiter goes next
			}
		}
		return false;
	}

	void Reset()
	{
		Holder.Reset();
		Waiting.Reset();
	}

	TWeakObjectPtr<T> Holder;
	TArray<TWeakObjectPtr<T>> Waiting;
};
