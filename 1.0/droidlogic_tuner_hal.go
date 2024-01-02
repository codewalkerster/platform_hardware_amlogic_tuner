package droidlogic_tuner_hal

import (
    //"fmt"
    "android/soong/android"
    "android/soong/cc"
)

func init() {
    android.RegisterModuleType("droidlogic_tuner_hal_defaults", tuner_hal_DefaultsFactory)
}

func tuner_hal_DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, func(ctx android.LoadHookContext) {
        type props struct {
            Cflags []string
        }
        p := &props{}
        p.Cflags = globalDefaults(ctx)
        ctx.AppendProperties(p)
    })

    return module
}

func globalDefaults(ctx android.BaseContext) ([]string) {
    var cppflags []string
    targetProduct := ctx.AConfig().Getenv("TARGET_PRODUCT")
    switch targetProduct {
        case "ohm", "ohmcas", "ohm_hybrid", "ohm_cbs":
            //fmt.Printf("TARGET_PRODUCT is %s, define SUPPORT_TSD\n", targetProduct)
            cppflags = append(cppflags,"-DSUPPORT_TSD")
        default:
    }
    return cppflags
}
