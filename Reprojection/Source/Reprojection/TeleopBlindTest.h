#pragma once

#include "CoreMinimal.h"
#include "TeleopTypes.h"

class FTeleopLogger;

/**
 * Blind test mode (HANDOFF §8, menu button): the HUD hides the method name, conditions (M0-M4 at the current
 * latency / depth presets) run in random order for teleop.Blind.ConditionSec each, then the operator rates
 * stability, comfort and depth credibility on 1-7 with the stick (A confirms). Ratings go to blind.csv.
 */
class FTeleopBlindTest
{
public:
	enum class EPhase : uint8 { Idle, Viewing, Rating, Done };

	void Start(double Now, FTeleopLogger* InLogger);
	void Stop();
	bool IsActive() const { return Phase == EPhase::Viewing || Phase == EPhase::Rating; }
	EPhase GetPhase() const { return Phase; }

	void Tick(double Now);

	/** Stick up / down while rating. */
	void AdjustRating(int32 Delta);

	/** A: while viewing ends the condition early; while rating confirms the current question. */
	void Confirm(double Now);

	/** Method to present now (valid while active). */
	ETeleopMethod GetMethod() const;

	FString GetConditionCode() const;
	FString GetHudText(double Now) const;

private:
	void WriteResult(double Now);

	TArray<ETeleopMethod> Conditions;
	int32 Index = 0;
	EPhase Phase = EPhase::Idle;
	double ViewStart = 0.0;
	double ViewEnd = 0.0;
	int32 Question = 0;
	int32 Ratings[3] = { 4, 4, 4 };
	int32 Value = 4;
	FTeleopLogger* Logger = nullptr;
};
