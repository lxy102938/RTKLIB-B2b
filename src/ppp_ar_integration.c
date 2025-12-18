/*------------------------------------------------------------------------------
* ppp_ar_integration.c : Integration wrapper for Pass-by-Pass AR
*
* This file provides integration between the main processing loop and AR functions
*
* version : $Revision:$ $Date:$
* history : 2025/01/xx  1.0  new
*-----------------------------------------------------------------------------*/
#include "rtklib.h"
#include "ppp_ar_passbypass.h"

/* global AR data - shared across processing */
extern satamb_t satamb[MAXSAT];
extern int n_ddamb;
extern ddamb_t ddamb[MAXSAT*MAXSAT];
extern int refsat;

/* AR processing wrapper for 48h PPP-B2b -------------------------------------
* This function should be called after both days of processing are complete
* args   : prcopt_t *popt  I   processing options
*          rtk_t    *rtk   IO  RTK control struct (final state after day 2)
*          obs_t    *obs   I   all observations (both days)
* return : number of fixed ambiguities, 0 if AR disabled or failed
*-----------------------------------------------------------------------------*/
extern int ppp_ar_48h(const prcopt_t *popt, rtk_t *rtk, const obs_t *obs)
{
    int n_fixed = 0;

    /* check if AR is enabled */
    if (popt->armode_pbp == 0) {
        trace(2, "ppp_ar_48h: Pass-by-Pass AR is disabled\n");
        return 0;
    }

    trace(1, "\n========== Pass-by-Pass Ambiguity Resolution ==========\n");
    printf("\n========== Pass-by-Pass Ambiguity Resolution ==========\n");

    /* Step 1: ambiguities should already be collected during processing */
    /* Print summary */
    print_arc_summary();

    /* Step 2: select reference satellite */
    if (popt->pbp_refsat == 0) {
        /* auto-select: choose satellite with both day0 and day1 data and most total observations */
        int max_obs = 0;
        int has_day0, has_day1;

        for (int i = 0; i < MAXSAT; i++) {
            if (satamb[i].n == 0) continue;

            /* check if this satellite has both day 0 and day 1 arcs with enough obs */
            has_day0 = 0;
            has_day1 = 0;
            int total_obs = 0;

            for (int j = 0; j < satamb[i].n; j++) {
                if (satamb[i].arc[j].day == 0 && satamb[i].arc[j].nobs >= 10) {
                    has_day0 = 1;
                    total_obs += satamb[i].arc[j].nobs;
                }
                if (satamb[i].arc[j].day == 1 && satamb[i].arc[j].nobs >= 10) {
                    has_day1 = 1;
                    total_obs += satamb[i].arc[j].nobs;
                }
            }

            /* only consider satellites with both days */
            if (has_day0 && has_day1 && total_obs > max_obs) {
                max_obs = total_obs;
                refsat = i + 1;
            }
        }

        if (refsat == 0) {
            trace(1, "ppp_ar_48h: no satellite has both day 0 and day 1 data\n");
            printf("\n========================================\n");
            printf("ERROR: Pass-by-Pass AR 失败\n");
            printf("========================================\n");
            printf("原因: 没有卫星同时拥有day 0和day 1的数据\n");
            printf("要求: 每天至少10个观测\n\n");
            printf("可能的原因:\n");
            printf("1. 数据只覆盖一天 - 检查观测文件和B2b文件时间范围\n");
            printf("2. PPP解算质量差 - 检查解算状态是否为SOLQ_PPP\n");
            printf("3. Day编号计算错误 - 查看上面的DEBUG输出\n");
            printf("4. 数据中间有大间隙 - day编号可能跳过1直接到2\n\n");
            printf("建议:\n");
            printf("- 查看上面的'Arc distribution'行，确认day0和day1都有数据\n");
            printf("- 查看DEBUG输出，确认current_day从0变为1\n");
            printf("- 检查是否看到'AR: Day 1 started'消息\n");
            printf("========================================\n\n");
            return 0;
        }
    } else {
        refsat = popt->pbp_refsat;
    }

    char satid[8];
    satno2id(refsat, satid);
    trace(1, "Reference satellite: %s\n", satid);
    printf("Reference satellite: %s\n", satid);

    /* Step 3: compute DD ambiguities */
    if (!compute_dd_ambiguities(satamb, refsat, ddamb, &n_ddamb)) {
        trace(1, "ppp_ar_48h: failed to compute DD ambiguities\n");
        printf("Error: Failed to compute DD ambiguities\n");
        return 0;
    }

    trace(1, "DD ambiguities computed: %d\n", n_ddamb);
    printf("DD ambiguities computed: %d\n", n_ddamb);

    /* Step 4: fix WL and NL ambiguities */
    n_fixed = fix_wl_nl_ambiguities(ddamb, n_ddamb);

    trace(1, "Fixed ambiguities: %d\n", n_fixed);
    printf("Fixed ambiguities: %d/%d\n", n_fixed, n_ddamb);

    if (n_fixed == 0) {
        trace(1, "ppp_ar_48h: no ambiguities fixed\n");
        printf("Warning: No ambiguities could be fixed\n");
        return 0;
    }

    /* Step 5: apply AR constraints if full AR mode */
    if (popt->armode_pbp >= 3) {
        int n_applied = apply_ar_fixed(rtk, ddamb, n_ddamb);
        trace(1, "AR constraints applied: %d\n", n_applied);
        printf("AR constraints applied: %d\n", n_applied);

        if (n_applied > 0) {
            trace(1, "Solution status: FIXED\n");
            printf("Solution status: FIXED\n");
        }
    }

    trace(1, "=======================================================\n\n");
    printf("=======================================================\n\n");

    return n_fixed;
}

/* collect ambiguities during epoch processing ----------------------------------
* This should be called for each epoch during PPP processing
* args   : rtk_t   *rtk    I   RTK control struct
*          obsd_t  *obs    I   observation data for current epoch
*          int     n       I   number of observations
*          int     day     I   day number (0=day1, 1=day2)
* return : number of ambiguities collected
*-----------------------------------------------------------------------------*/
extern int collect_ambiguities_epoch(const rtk_t *rtk, const obsd_t *obs, int n, int day)
{
    extern satamb_t satamb[];
    return collect_ambiguities(rtk, obs, n, day, satamb);
}
