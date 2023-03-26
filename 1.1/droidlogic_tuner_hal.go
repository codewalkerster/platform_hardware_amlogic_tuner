package droidlogic_tuner_hal_1_1

import (
    "android/soong/android"
    "android/soong/cc"
    "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("droidlogic_tuner_hal_1_1_defaults", tuner_hal_DefaultsFactory)
}

func tuner_hal_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, func(ctx android.LoadHookContext) {
        type props struct {
            Cflags []string
            Enabled *bool
        }
        p := &props{}
        p.Cflags = globalDefaults(ctx)
        PlatformVndkVersion := ctx.DeviceConfig().PlatformVndkVersion()
        if PlatformVndkVersion == "30" {
            p.Enabled = proptools.BoolPtr(false)
        } else {
            p.Enabled = proptools.BoolPtr(true)
        }
        ctx.AppendProperties(p)
    })

    return module
}

func globalDefaults(ctx android.BaseContext) ([]string) {
    var cppflags []string
    targetProduct := ctx.AConfig().Getenv("TARGET_PRODUCT")
    switch targetProduct {
        case "ohm", "ohmcas", "ohm_hybrid", "ohm_cbs":
            cppflags = append(cppflags,"-DSUPPORT_TSD")
        default:
    }
    return cppflags
}
