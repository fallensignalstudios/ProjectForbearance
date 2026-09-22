// Unreal Engine 5 module rules for the Sovereign Call: Expansion simulation.
//
// NOT COMPILED HERE. This repository builds with CMake and Ninja in a container
// with no engine in it, so nothing in this file has been verified by a compiler.
// What has been verified is the arrangement it depends on: the simulation
// compiles and passes its whole test suite as one translation unit, under the
// same strict warnings, as the `expansion_tests_amalgamated` target proves on
// every build. See docs/decisions/0010-unreal-host.md and
// docs/unreal_integration.md.
using UnrealBuildTool;
using System.IO;

public class ExpansionUE : ModuleRules
{
    public ExpansionUE(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        // The core raises SimError internally and api.hpp turns every escape into
        // a typed Outcome. Those catch handlers are on this side of the module
        // boundary and are the reason nothing propagates outward, so this module
        // needs exceptions even though no exception ever leaves it. Without this
        // the try/catch in api.cpp does not compile.
        bEnableExceptions = true;

        // The simulation uses no dynamic_cast and no typeid, so RTTI stays off.

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "UMG",
            "Slate",
            "SlateCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Projects",   // IPluginManager, for locating shipped content
        });

        // The repository root, four levels up from
        // <repo>/hosts/unreal/Source/ExpansionUE.
        string Repo = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", ".."));

        PublicIncludePaths.AddRange(new string[]
        {
            Path.Combine(Repo, "core", "include"),
            Path.Combine(Repo, "persistence", "include"),
            Path.Combine(Repo, "presentation", "include"),
        });

        // The simulation's .cpp files include one another's private headers by a
        // path relative to the repository root, exactly as they do in the CMake
        // build.
        PrivateIncludePaths.Add(Repo);

        // hostfs/ is deliberately absent. It is the only module that opens a
        // file, and a packaged build serves content through FExpansionPakContent
        // instead. Adding it here would hand the simulation a filesystem it is
        // designed not to have.

        // Private/ExpansionAmalgamated.cpp is the one file that compiles the
        // simulation: UnrealBuildTool compiles what it finds under the module
        // directory, and the simulation lives outside it. tools/check_amalgamation.py
        // fails the build if that file and the source tree disagree.

        // The simulation carries its own warning discipline and is built clean
        // under -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        // -Werror, so it should arrive without a single suppression. If the
        // engine's default set reports something new, fix the source rather than
        // silencing it here.
    }
}
