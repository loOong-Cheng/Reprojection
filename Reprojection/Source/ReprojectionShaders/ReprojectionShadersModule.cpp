#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

/**
 * Maps <Project>/Shaders to "/Reprojection" so material Custom nodes can
 * #include "/Reprojection/Teleop/....ush". Shader code then lives in plain text files under git.
 */
class FReprojectionShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const FString ShaderDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
		AddShaderSourceDirectoryMapping(TEXT("/Reprojection"), ShaderDir);
	}
};

IMPLEMENT_MODULE(FReprojectionShadersModule, ReprojectionShaders);
