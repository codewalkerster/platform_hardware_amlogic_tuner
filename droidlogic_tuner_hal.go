package droidlogic_tuner_hal

import (
    "fmt"
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
    if ctx.AConfig().Getenv("TARGET_PRODUCT") == "ohm" {
          fmt.Printf("TARGET_PRODUCT is ohm, define SUPPORT_TSD\n")
          cppflags = append(cppflags,"-DSUPPORT_TSD")
    }
    if ctx.AConfig().Getenv("TARGET_PRODUCT") == "ohmicas" {
          fmt.Printf("TARGET_PRODUCT is ohmcas, define SUPPORT_TSD\n")
          cppflags = append(cppflags,"-DSUPPORT_TSD")
    }

    if ctx.AConfig().Getenv("TARGET_PRODUCT") == "ohm_hybrid" {
          fmt.Printf("TARGET_PRODUCT is ohmcas, define SUPPORT_TSD\n")
          cppflags = append(cppflags,"-DSUPPORT_TSD")
    }
    return cppflags
}
