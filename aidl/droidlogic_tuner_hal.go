package droidlogic_tuner_hal_aidl

import (
    "fmt"
    "android/soong/android"
    "android/soong/cc"
    "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("droidlogic_tuner_hal_aidl_defaults", tuner_hal_DefaultsFactory)
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
        if PlatformVndkVersion == "30" || PlatformVndkVersion == "31" {
            //fmt.Println("Disable tunerhal aidl")
            p.Enabled = proptools.BoolPtr(false)
        } else {
            //fmt.Println("Enable tunerhal aidl")
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
            fmt.Printf("TARGET_PRODUCT is %s, define SUPPORT_TSD\n", targetProduct)
            cppflags = append(cppflags,"-DSUPPORT_TSD")
        default:
    }

    CBSSrcPath := "vendor/google/tv/broadcaststack"
    if android.ExistentPathForSource(ctx, CBSSrcPath).Valid() == true {
        //fmt.Printf("cbs:%s exist, define support cbs\n", CBSSrcPath)
        cppflags = append(cppflags,"-DSUPPORT_CBS")
    }
    return cppflags
}
