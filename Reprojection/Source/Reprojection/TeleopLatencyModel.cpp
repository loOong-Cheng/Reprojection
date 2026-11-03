#include "TeleopLatencyModel.h"

#include "Reprojection.h"
#include "TeleopCVars.h"

#include "Algo/Count.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// HANDOFF §4 presets. Jitter "small / medium / large / very large" -> 2 / 6 / 15 / 30 ms stationary std.
	const FTeleopLatencyPreset GPresets[FTeleopLatencyModel::NumNetworkPresets] = {
		{ TEXT("Ideal"),    0.f,   0.f,  0.f, 0.00f, 1.00f, 0.000f, 0.000f },
		{ TEXT("LAN"),     10.f,  25.f,  2.f, 0.01f, 0.30f, 0.000f, 0.005f },
		{ TEXT("WiFi"),    25.f,  60.f,  6.f, 0.03f, 0.20f, 0.005f, 0.030f },
		{ TEXT("Remote"),  60.f, 150.f, 15.f, 0.05f, 0.15f, 0.010f, 0.080f },
		{ TEXT("Bad"),    120.f, 300.f, 30.f, 0.10f, 0.10f, 0.030f, 0.150f },
	};
}

const FTeleopLatencyPreset& FTeleopLatencyModel::GetPreset(int32 Index)
{
	return GPresets[FMath::Clamp(Index, 0, NumNetworkPresets - 1)];
}

FString FTeleopLatencyModel::GetPresetName(int32 Index)
{
	return Index == ConstantPreset ? FString(TEXT("Constant")) : FString(GetPreset(Index).Name);
}

FTeleopLatencyModel::FTeleopLatencyModel(int32 Seed)
	: Rng(Seed)
{
}

double FTeleopLatencyModel::SampleJitterInnovation(double StdMs)
{
	if (StdMs <= 0.0)
	{
		return 0.0;
	}
	// eps = a * (X - E[X]), X ~ LogNormal(0, s), scaled so the AR(1) process has stationary std StdMs.
	const double S2 = LogNormalSigma * LogNormalSigma;
	const double Mean = FMath::Exp(0.5 * S2);
	const double Var = (FMath::Exp(S2) - 1.0) * FMath::Exp(S2);
	const double InnovationStd = StdMs * FMath::Sqrt(1.0 - Rho * Rho);
	const double A = InnovationStd / FMath::Sqrt(Var);

	// Standard normal via Box-Muller.
	const double U1 = FMath::Max(Rng.GetFraction(), 1e-12);
	const double U2 = Rng.GetFraction();
	const double N = FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(2.0 * PI * U2);
	return A * (FMath::Exp(LogNormalSigma * N) - Mean);
}

FTeleopLatencyModel::FFrameSample FTeleopLatencyModel::SampleFrame()
{
	return SampleFrame(TeleopCVars::LatencyPreset.GetValueOnAnyThread(), TeleopCVars::LatencyBaseOffsetMs.GetValueOnAnyThread());
}

FTeleopLatencyModel::FFrameSample FTeleopLatencyModel::SampleFrame(int32 PresetIndex, float BaseOffsetMs)
{
	FFrameSample Sample;
	if (PresetIndex == ConstantPreset)
	{
		Sample.LatencySeconds = FMath::Max(0.f, TeleopCVars::LatencyMs.GetValueOnAnyThread()) / 1000.0;
		return Sample;
	}

	const FTeleopLatencyPreset& P = GetPreset(PresetIndex);
	if (PresetIndex != LastPreset)
	{
		bBad = false;
		JitterMs = 0.0;
		LastPreset = PresetIndex;
	}

	// Gilbert-Elliott state transition (per frame).
	bBad = bBad ? (Rng.GetFraction() >= P.PBadToGood) : (Rng.GetFraction() < P.PGoodToBad);

	// AR(1) correlated jitter.
	JitterMs = Rho * JitterMs + SampleJitterInnovation(P.JitterStdMs);

	const double Base = (bBad ? P.BaseBadMs : P.BaseGoodMs) + BaseOffsetMs;
	const double NetMs = FMath::Max(0.0, Base + JitterMs);
	const double EncMs = EncodeBaseMs + Rng.FRandRange(0.f, EncodeJitterMs);
	const double TotalMs = ExposureMs + EncMs + NetMs + DecodeMs + DisplayMs;

	Sample.LatencySeconds = TotalMs / 1000.0;
	Sample.bBadState = bBad;
	Sample.bDropped = Rng.GetFraction() < (bBad ? P.LossBad : P.LossGood);
	return Sample;
}

void FTeleopLatencyModel::TickCommandChannel(float DeltaSeconds)
{
	const int32 PresetIndex = TeleopCVars::LatencyPreset.GetValueOnGameThread();
	if (PresetIndex == ConstantPreset)
	{
		CmdJitterMs = 0.0;
		return;
	}
	// Smaller, slower jitter than the video path: 30 % of the preset jitter, correlation time ~0.2 s.
	const double StdMs = 0.3 * GetPreset(PresetIndex).JitterStdMs;
	const double RhoTick = FMath::Exp(-FMath::Max(DeltaSeconds, 1e-3) / 0.2);
	const double Innovation = StdMs * FMath::Sqrt(1.0 - RhoTick * RhoTick);
	const double U1 = FMath::Max(Rng.GetFraction(), 1e-12);
	const double U2 = Rng.GetFraction();
	CmdJitterMs = RhoTick * CmdJitterMs + Innovation * FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(2.0 * PI * U2);
}

double FTeleopLatencyModel::GetCommandLatencySeconds() const
{
	const double BaseMs = FMath::Max(0.f, TeleopCVars::CmdLatencyMs.GetValueOnAnyThread());
	return FMath::Max(0.0, BaseMs + CmdJitterMs) / 1000.0;
}

// teleop.LatencyTest [preset] [frames]: sample the model offline and print the distribution (S4 check).
static FAutoConsoleCommand GTeleopLatencyTest(
	TEXT("teleop.LatencyTest"),
	TEXT("teleop.LatencyTest [preset 0-4, default all] [frames, default 36000]: print p50/p95/p99, loss and a histogram of the latency model."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const int32 Frames = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 36000;
		const int32 First = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		const int32 Last = Args.Num() > 0 ? First : FTeleopLatencyModel::NumNetworkPresets - 1;
		for (int32 Preset = First; Preset <= Last; ++Preset)
		{
			FTeleopLatencyModel Model(1000 + Preset);
			TArray<double> Ms;
			Ms.Reserve(Frames);
			int32 Dropped = 0;
			int32 Bad = 0;
			for (int32 i = 0; i < Frames; ++i)
			{
				const FTeleopLatencyModel::FFrameSample S = Model.SampleFrame(Preset, 0.f);
				Ms.Add(S.LatencySeconds * 1000.0);
				Dropped += S.bDropped ? 1 : 0;
				Bad += S.bBadState ? 1 : 0;
			}
			Ms.Sort();
			auto Pct = [&Ms](double P) { return Ms[FMath::Clamp(int32(P * (Ms.Num() - 1)), 0, Ms.Num() - 1)]; };
			FString Hist;
			for (int32 Bin = 0; Bin < 40; ++Bin)
			{
				const double Lo = Bin * 10.0;
				const int32 Count = Algo::CountIf(Ms, [Lo](double V) { return V >= Lo && V < Lo + 10.0; });
				if (Count > 0)
				{
					Hist += FString::Printf(TEXT(" %d-%d:%.1f%%"), int32(Lo), int32(Lo + 10), 100.0 * Count / Frames);
				}
			}
			UE_LOG(LogTeleop, Display, TEXT("[latency] %s: p50=%.1f p95=%.1f p99=%.1f min=%.1f max=%.1f ms, loss=%.2f%%, bad state=%.1f%% |%s"),
				*FTeleopLatencyModel::GetPresetName(Preset), Pct(0.5), Pct(0.95), Pct(0.99), Ms[0], Ms.Last(),
				100.0 * Dropped / Frames, 100.0 * Bad / Frames, *Hist);
		}
	}));
