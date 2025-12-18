/*------------------------------------------------------------------------------
* ppp_ar_passbypass.c : PPP ambiguity resolution using pass-by-pass method
*
* reference :
*    Pass-by-Pass Ambiguity Resolution in Single GPS Receiver PPP Using
*    Observations for Two Sequential Days: An Exploratory Study
*
* version : $Revision:$ $Date:$
* history : 2025/01/xx  1.0  new
*-----------------------------------------------------------------------------*/
#include "rtklib.h"
#include "ppp_ar_passbypass.h"

#define CLIGHT      299792458.0     /* speed of light (m/s) */
#define FREQ1       1.57542E9       /* L1/E1 frequency (Hz) */
#define FREQ2       1.22760E9       /* L2 frequency (Hz) */

/* get wavelength ---------------------------------------------------------------
* get carrier wavelength
* args   : int freq        I   frequency (1:L1, 2:L2)
* return : wavelength (m) (0.0: error)
*-----------------------------------------------------------------------------*/
static double lam_carr(int freq)
{
    if (freq == 1) return CLIGHT / FREQ1;
    if (freq == 2) return CLIGHT / FREQ2;
    return 0.0;
}

/* HMW (Hatch-Melbourne-Wübbena) combination ------------------------------------
* compute HMW combination for wide-lane ambiguity estimation
* args   : double L1       I   L1 carrier phase (cycles)
*          double L2       I   L2 carrier phase (cycles)
*          double P1       I   P1 pseudorange (m)
*          double P2       I   P2 pseudorange (m)
* return : HMW value (cycles)
*          WL ambiguity = (L1 - L2) - (f1*P1 + f2*P2)/(f1+f2) / lambda_WL
*-----------------------------------------------------------------------------*/
extern double hmw_combination(double L1, double L2, double P1, double P2)
{
    double f1 = FREQ1, f2 = FREQ2;
    double lam1 = lam_carr(1), lam2 = lam_carr(2);
    double lam_WL = CLIGHT / (f1 - f2);  /* wide-lane wavelength */
    double N_WL;

    /* WL phase in meters */
    double phi_WL = lam1 * L1 - lam2 * L2;

    /* MW code combination in meters */
    double P_MW = (f1 * P1 + f2 * P2) / (f1 + f2);

    /* HMW: N_WL = (phi_WL - P_MW) / lam_WL */
    N_WL = (phi_WL - P_MW) / lam_WL;

    return N_WL;
}

/* compute NL (narrow-lane) ambiguity from IF and WL ----------------------------
* NL ambiguity = (f1*N1 + f2*N2)/(f1+f2)
*               = (f1/(f1-f2))*N_IF/lam1 - (f2/(f1-f2))*N_WL
* args   : double N_IF     I   IF ambiguity (m)
*          double N_WL     I   WL ambiguity (cycles)
* return : NL ambiguity (cycles)
*-----------------------------------------------------------------------------*/
extern double nl_from_if_wl(double N_IF, double N_WL)
{
    double f1 = FREQ1, f2 = FREQ2;
    double lam1 = lam_carr(1);
    double N_NL;

    /* N_NL = (f1/(f1-f2)) * N_IF/lam1 - (f2/(f1-f2)) * N_WL */
    N_NL = (f1 / (f1 - f2)) * (N_IF / lam1) - (f2 / (f1 - f2)) * N_WL;

    return N_NL;
}

/* compute IF ambiguity from WL and NL ------------------------------------------
* N_IF = (f1*lam1*N1 - f2*lam2*N2)/(f1-f2)
*      = lam1*(f1*N_NL/(f1+f2) + f2*N_WL/(f1-f2))
* args   : double N_WL     I   WL ambiguity (cycles)
*          double N_NL     I   NL ambiguity (cycles)
* return : IF ambiguity (m)
*-----------------------------------------------------------------------------*/
extern double if_from_wl_nl(double N_WL, double N_NL)
{
    double f1 = FREQ1, f2 = FREQ2;
    double lam1 = lam_carr(1);
    double N_IF;

    /* N_IF = lam1 * (f1*N_NL/(f1+f2) + f2*N_WL/(f1-f2)) */
    N_IF = lam1 * (f1 * N_NL / (f1 + f2) + f2 * N_WL / (f1 - f2));

    return N_IF;
}

/* round to nearest integer -----------------------------------------------------*/
static int NINT(double x)
{
    return (int)floor(x + 0.5);
}

/* fix ambiguity to nearest integer with threshold check ------------------------
* args   : double N_float  I   float ambiguity
*          double std      I   standard deviation
*          double threshold I  threshold for fixing (cycles)
*          int    *fixed   O   fix flag (1=fixed, 0=not fixed)
* return : fixed integer ambiguity
*-----------------------------------------------------------------------------*/
extern int fix_ambiguity(double N_float, double std, double threshold, int *fixed)
{
    double frac = fabs(N_float - NINT(N_float));
    int N_fix = NINT(N_float);

    /* check if fraction is small enough and std is small */
    if (frac < threshold && std < threshold) {
        *fixed = 1;
        return N_fix;
    }

    *fixed = 0;
    return N_fix;  /* return nearest integer even if not fixed */
}

/* ratio test for ambiguity validation ------------------------------------------
* args   : double *cand    I   candidates (sorted by residuals)
*          int    n        I   number of candidates
*          double threshold I  ratio threshold
* return : 1=pass, 0=fail
*-----------------------------------------------------------------------------*/
extern int ratio_test(const double *cand, int n, double threshold)
{
    if (n < 2) return 0;
    if (cand[1] <= 0.0) return 0;
    return (cand[1] / cand[0]) > threshold;
}

/* initialize ambiguity arc data ------------------------------------------------*/
extern void init_arc_data(void)
{
    int i;
    extern satamb_t satamb[];

    for (i = 0; i < MAXSAT; i++) {
        satamb[i].n = 0;
    }
}

/* print ambiguity arc summary --------------------------------------------------*/
extern void print_arc_summary(void)
{
    extern satamb_t satamb[];
    int i, j, total_arcs = 0;
    int sats_with_both_days = 0;
    int arcs_day0 = 0, arcs_day1 = 0, arcs_other = 0;
    char satid[8];

    printf("\n========== Ambiguity Arc Summary ==========\n");

    /* First pass: count arcs per day to diagnose day assignment */
    for (i = 0; i < MAXSAT; i++) {
        for (j = 0; j < satamb[i].n; j++) {
            if (satamb[i].arc[j].day == 0) arcs_day0++;
            else if (satamb[i].arc[j].day == 1) arcs_day1++;
            else arcs_other++;
        }
    }

    printf("Arc distribution: day0=%d, day1=%d, other=%d\n",
           arcs_day0, arcs_day1, arcs_other);

    for (i = 0; i < MAXSAT; i++) {
        if (satamb[i].n == 0) continue;
        satno2id(i + 1, satid);

        /* check if has both days */
        int has_day0 = 0, has_day1 = 0;
        for (j = 0; j < satamb[i].n; j++) {
            if (satamb[i].arc[j].day == 0 && satamb[i].arc[j].nobs >= 10) has_day0 = 1;
            if (satamb[i].arc[j].day == 1 && satamb[i].arc[j].nobs >= 10) has_day1 = 1;
        }
        if (has_day0 && has_day1) sats_with_both_days++;

        printf("%s: %d arcs [%s]\n", satid, satamb[i].n,
               (has_day0 && has_day1) ? "day0+day1" : (has_day0 ? "day0 only" : "day1 only"));

        for (j = 0; j < satamb[i].n; j++) {
            printf("  Arc %d: day=%d, nobs=%d, N_IF=%.3f±%.3f, N_WL=%.3f±%.3f\n",
                   j, satamb[i].arc[j].day, satamb[i].arc[j].nobs,
                   satamb[i].arc[j].N_IF, sqrt(satamb[i].arc[j].var_IF),
                   satamb[i].arc[j].N_WL, sqrt(satamb[i].arc[j].var_WL));
        }
        total_arcs += satamb[i].n;
    }
    printf("Total arcs: %d\n", total_arcs);
    printf("Satellites with both day0 and day1 (>=10 obs): %d\n", sats_with_both_days);
    printf("==========================================\n\n");
}

/* collect ambiguities from RTK solution ----------------------------------------
* collect IF ambiguity estimates and compute WL ambiguities from observations
* args   : rtk_t   *rtk    I   RTK control struct
*          obsd_t  *obs    I   observation data
*          int     n       I   number of observations
*          int     day     I   day number (0=day1, 1=day2)
*          satamb_t *satamb IO satellite ambiguity data
* return : number of ambiguities collected
*-----------------------------------------------------------------------------*/
extern int collect_ambiguities(const rtk_t *rtk, const obsd_t *obs, int n,
                                 int day, satamb_t *satamb)
{
    int i, sat, idx_IF, idx_arc;
    double N_IF, var_IF, N_WL, L1, L2, P1, P2;
    gtime_t time;
    char satid[8];
    int count = 0;

    trace(3, "collect_ambiguities: n=%d day=%d\n", n, day);

    for (i = 0; i < n; i++) {
        sat = obs[i].sat;
        time = obs[i].time;

        /* get L1/L2 observations */
        L1 = obs[i].L[0];  /* cycles */
        L2 = obs[i].L[1];  /* cycles */
        P1 = obs[i].P[0];  /* meters */
        P2 = obs[i].P[1];  /* meters */

        /* check validity */
        if (L1 == 0.0 || L2 == 0.0 || P1 == 0.0 || P2 == 0.0) continue;

        /* get IF ambiguity from filter state (assuming NF=2, freq 0 is used for IF) */
        /* In PPP, ambiguity index is IB(sat, freq, opt) = NR(opt) + MAXSAT*freq + (sat-1) */
        /* For IF combination, we use the first frequency's ambiguity slot */
        idx_IF = rtk->opt.dynamics ? 9 : 3;  /* skip position states */
        idx_IF += 1;  /* skip clock state (simplified, may need adjustment for multiple systems) */
        idx_IF += (rtk->opt.tropopt < TROPOPT_EST) ? 0 : (rtk->opt.tropopt >= TROPOPT_ESTG ? 3 : 1);
        idx_IF += 0;  /* skip ionosphere (assuming iono-free) */
        idx_IF += MAXSAT * 0 + (sat - 1);  /* ambiguity for sat, freq 0 */

        N_IF = rtk->x[idx_IF];
        var_IF = rtk->P[idx_IF + idx_IF * rtk->nx];

        /* skip if ambiguity not initialized */
        if (N_IF == 0.0) continue;

        /* compute WL ambiguity using HMW */
        N_WL = hmw_combination(L1, L2, P1, P2);

        satno2id(sat, satid);
        trace(4, "  %s: N_IF=%.3f var=%.4f N_WL=%.3f\n",
              satid, N_IF, var_IF, N_WL);

        /* find or create arc for this satellite */
        idx_arc = -1;
        for (int j = 0; j < satamb[sat - 1].n; j++) {
            if (satamb[sat - 1].arc[j].day == day) {
                /* check if this is continuation of existing arc */
                double dt = timediff(time, satamb[sat - 1].arc[j].te);
                if (dt < 300.0) {  /* within 5 minutes, same arc */
                    idx_arc = j;
                    break;
                }
            }
        }

        if (idx_arc < 0) {
            /* create new arc */
            if (satamb[sat - 1].n >= MAXARC) {
                trace(2, "Warning: too many arcs for satellite %s\n", satid);
                continue;
            }
            idx_arc = satamb[sat - 1].n++;
            satamb[sat - 1].arc[idx_arc].ts = time;
            satamb[sat - 1].arc[idx_arc].sat = sat;
            satamb[sat - 1].arc[idx_arc].day = day;
            satamb[sat - 1].arc[idx_arc].N_IF = N_IF;
            satamb[sat - 1].arc[idx_arc].var_IF = var_IF;
            satamb[sat - 1].arc[idx_arc].N_WL = N_WL;
            satamb[sat - 1].arc[idx_arc].var_WL = 0.25;  /* initial WL variance (cycles^2) */
            satamb[sat - 1].arc[idx_arc].nobs = 1;
            satamb[sat - 1].arc[idx_arc].fixed_WL = 0;
            satamb[sat - 1].arc[idx_arc].fixed_NL = 0;

            trace(3, "New arc created for %s day %d: N_IF=%.3f N_WL=%.3f\n",
                  satid, day, N_IF, N_WL);
        } else {
            /* update existing arc with running average */
            ambarc_t *arc = &satamb[sat - 1].arc[idx_arc];
            int n_old = arc->nobs;
            arc->te = time;
            arc->N_IF = (arc->N_IF * n_old + N_IF) / (n_old + 1);
            arc->var_IF = (arc->var_IF * n_old + var_IF) / (n_old + 1);
            arc->N_WL = (arc->N_WL * n_old + N_WL) / (n_old + 1);
            arc->var_WL = arc->var_WL * 0.99 + 0.01;  /* slowly increase variance */
            arc->nobs++;
        }
        count++;
    }

    trace(3, "collect_ambiguities: collected %d ambiguities\n", count);
    return count;
}

/* compute single-difference ambiguities across days ----------------------------
* for same satellite, compute SD between day1 and day2 arcs
* this removes satellite-dependent biases
* args   : satamb_t *satamb I   satellite ambiguity data
*          int      sat     I   satellite number
*          double   *SD_WL  O   SD WL ambiguity (cycles)
*          double   *var_SD O   SD WL variance
* return : 1=success, 0=fail
*-----------------------------------------------------------------------------*/
static int compute_sd_wl_across_days(const satamb_t *satamb, int sat,
                                       double *SD_WL, double *var_SD)
{
    int arc_day0 = -1, arc_day1 = -1;
    int i;
    int max_obs_day0 = 0, max_obs_day1 = 0;

    /* find best arcs for day 0 and day 1 (arc with most observations) */
    for (i = 0; i < satamb[sat - 1].n; i++) {
        if (satamb[sat - 1].arc[i].day == 0 && satamb[sat - 1].arc[i].nobs >= 10) {
            if (satamb[sat - 1].arc[i].nobs > max_obs_day0) {
                max_obs_day0 = satamb[sat - 1].arc[i].nobs;
                arc_day0 = i;
            }
        }
        if (satamb[sat - 1].arc[i].day == 1 && satamb[sat - 1].arc[i].nobs >= 10) {
            if (satamb[sat - 1].arc[i].nobs > max_obs_day1) {
                max_obs_day1 = satamb[sat - 1].arc[i].nobs;
                arc_day1 = i;
            }
        }
    }

    if (arc_day0 < 0 || arc_day1 < 0) {
        return 0;  /* both days not available */
    }

    /* compute SD: day1 - day0 (removes satellite bias) */
    *SD_WL = satamb[sat - 1].arc[arc_day1].N_WL - satamb[sat - 1].arc[arc_day0].N_WL;
    *var_SD = satamb[sat - 1].arc[arc_day0].var_WL + satamb[sat - 1].arc[arc_day1].var_WL;

    return 1;
}

/* compute double-difference ambiguities ----------------------------------------
* compute DD ambiguities relative to reference satellite
* args   : satamb_t *satamb I   satellite ambiguity data
*          int      refsat  I   reference satellite number
*          ddamb_t  *ddamb  O   DD ambiguity data
*          int      *n_dd   O   number of DD ambiguities
* return : 1=success, 0=fail
*-----------------------------------------------------------------------------*/
extern int compute_dd_ambiguities(const satamb_t *satamb, int refsat,
                                   ddamb_t *ddamb, int *n_dd)
{
    int i, count = 0;
    double SD_WL_ref, var_SD_ref, SD_WL_sat, var_SD_sat;
    double DD_WL, var_DD_WL;
    char satid_ref[8], satid[8];

    *n_dd = 0;

    /* compute SD for reference satellite */
    if (!compute_sd_wl_across_days(satamb, refsat, &SD_WL_ref, &var_SD_ref)) {
        satno2id(refsat, satid_ref);
        trace(2, "Reference satellite %s: no valid SD WL\n", satid_ref);
        return 0;
    }

    satno2id(refsat, satid_ref);
    trace(3, "Reference satellite %s: SD_WL=%.3f±%.3f\n",
          satid_ref, SD_WL_ref, sqrt(var_SD_ref));

    /* compute DD for all other satellites */
    for (i = 1; i <= MAXSAT; i++) {
        if (i == refsat) continue;
        if (satamb[i - 1].n == 0) continue;

        /* compute SD for this satellite */
        if (!compute_sd_wl_across_days(satamb, i, &SD_WL_sat, &var_SD_sat)) {
            continue;
        }

        /* compute DD: SD_sat - SD_ref */
        DD_WL = SD_WL_sat - SD_WL_ref;
        var_DD_WL = var_SD_sat + var_SD_ref;

        /* store DD ambiguity */
        ddamb[count].sat1 = refsat;
        ddamb[count].sat2 = i;
        ddamb[count].DD_WL = DD_WL;
        ddamb[count].var_DD_WL = var_DD_WL;
        ddamb[count].fixed_WL = 0;
        ddamb[count].fixed_NL = 0;

        /* also compute DD for IF ambiguities */
        /* Using day2 arcs (assuming we want day2 solution) */
        int arc_ref = -1, arc_sat = -1;
        for (int j = 0; j < satamb[refsat - 1].n; j++) {
            if (satamb[refsat - 1].arc[j].day == 1) arc_ref = j;
        }
        for (int j = 0; j < satamb[i - 1].n; j++) {
            if (satamb[i - 1].arc[j].day == 1) arc_sat = j;
        }

        if (arc_ref >= 0 && arc_sat >= 0) {
            ddamb[count].DD_IF = satamb[i - 1].arc[arc_sat].N_IF -
                                  satamb[refsat - 1].arc[arc_ref].N_IF;
            ddamb[count].var_DD_IF = satamb[i - 1].arc[arc_sat].var_IF +
                                      satamb[refsat - 1].arc[arc_ref].var_IF;
            ddamb[count].arc1 = arc_ref;
            ddamb[count].arc2 = arc_sat;
        }

        satno2id(i, satid);
        trace(3, "DD %s-%s: DD_WL=%.3f±%.3f DD_IF=%.3f±%.3f\n",
              satid, satid_ref, DD_WL, sqrt(var_DD_WL),
              ddamb[count].DD_IF, sqrt(ddamb[count].var_DD_IF));

        count++;
    }

    *n_dd = count;
    trace(2, "compute_dd_ambiguities: %d DD ambiguities computed\n", count);
    return count > 0 ? 1 : 0;
}

/* fix WL and NL ambiguities ----------------------------------------------------
* fix DD WL and DD NL ambiguities to integers
* args   : ddamb_t  *ddamb  IO  DD ambiguity data
*          int      n_dd    I   number of DD ambiguities
* return : number of fixed ambiguities
*-----------------------------------------------------------------------------*/
extern int fix_wl_nl_ambiguities(ddamb_t *ddamb, int n_dd)
{
    int i, count_wl = 0, count_nl = 0;
    double threshold_wl = 0.15;  /* 0.15 cycles for WL */
    double threshold_nl = 0.15;  /* 0.15 cycles for NL */
    char satid1[8], satid2[8];

    trace(2, "fix_wl_nl_ambiguities: n_dd=%d\n", n_dd);

    for (i = 0; i < n_dd; i++) {
        double std_wl = sqrt(ddamb[i].var_DD_WL);
        int fixed;

        /* fix WL ambiguity */
        ddamb[i].DD_WL_fix = fix_ambiguity(ddamb[i].DD_WL, std_wl, threshold_wl, &fixed);
        ddamb[i].fixed_WL = fixed;
        if (fixed) count_wl++;

        satno2id(ddamb[i].sat1, satid1);
        satno2id(ddamb[i].sat2, satid2);

        if (fixed) {
            trace(3, "DD WL %s-%s: %.3f -> %d (std=%.3f) FIXED\n",
                  satid2, satid1, ddamb[i].DD_WL, (int)ddamb[i].DD_WL_fix, std_wl);

            /* compute NL from DD IF and fixed DD WL */
            double DD_NL = nl_from_if_wl(ddamb[i].DD_IF, ddamb[i].DD_WL_fix);
            double std_nl = sqrt(ddamb[i].var_DD_IF) / lam_carr(1) * 2.0;  /* rough estimate */

            /* fix NL ambiguity */
            ddamb[i].DD_NL = DD_NL;
            ddamb[i].var_DD_NL = std_nl * std_nl;
            ddamb[i].DD_NL_fix = fix_ambiguity(DD_NL, std_nl, threshold_nl, &fixed);
            ddamb[i].fixed_NL = fixed;
            if (fixed) {
                count_nl++;

                /* compute fixed DD IF from fixed WL and NL */
                ddamb[i].DD_IF_fix = if_from_wl_nl(ddamb[i].DD_WL_fix, ddamb[i].DD_NL_fix);

                trace(3, "DD NL %s-%s: %.3f -> %d (std=%.3f) FIXED, DD_IF_fix=%.3f\n",
                      satid2, satid1, DD_NL, (int)ddamb[i].DD_NL_fix, std_nl, ddamb[i].DD_IF_fix);
            }
        }
    }

    trace(2, "fix_wl_nl_ambiguities: WL fixed %d/%d, NL fixed %d/%d\n",
          count_wl, n_dd, count_nl, n_dd);

    return count_nl;  /* return number of fully fixed ambiguities */
}

/* apply AR fixed solution with pseudo-observations -----------------------------
* apply fixed DD ambiguities as pseudo-observations to constrain the solution
* args   : rtk_t    *rtk    IO  RTK control struct
*          ddamb_t  *ddamb  I   DD ambiguity data
*          int      n_dd    I   number of DD ambiguities
* return : number of constraints applied
* notes  : This implements Step 3 from the paper:
*          Construct pseudo-observation: v_b = D*b - N_DD_fixed
*          where D is the UD->DD mapping matrix, b are IF ambiguity parameters
*          Give it extremely large weight (sigma = 0.001 cycles ~ 0.0002 m)
*          Add to normal equation and re-solve
*-----------------------------------------------------------------------------*/
extern int apply_ar_fixed(rtk_t *rtk, const ddamb_t *ddamb, int n_dd)
{
    int i, count = 0;
    int idx_ref, idx_sat;
    double v_pseudo, sigma_pseudo, weight;
    double *H, *v, *R;
    int nx = rtk->nx;
    int nv = 0;
    char satid1[8], satid2[8];

    trace(2, "apply_ar_fixed: n_dd=%d\n", n_dd);

    /* count number of fixed ambiguities */
    for (i = 0; i < n_dd; i++) {
        if (ddamb[i].fixed_WL && ddamb[i].fixed_NL) count++;
    }

    if (count == 0) {
        trace(2, "apply_ar_fixed: no fixed ambiguities to apply\n");
        return 0;
    }

    /* allocate and initialize matrices for pseudo-observations */
    H = zeros(nx, count);      /* design matrix */
    v = zeros(count, 1);       /* residual vector */
    R = zeros(count, count);   /* measurement covariance */

    /* construct pseudo-observations */
    nv = 0;
    for (i = 0; i < n_dd; i++) {
        if (!ddamb[i].fixed_WL || !ddamb[i].fixed_NL) continue;

        /* compute ambiguity state indices */
        /* Ambiguity index: IB(sat, freq, opt) = NR(opt) + MAXSAT*freq + (sat-1) */
        idx_ref = (rtk->opt.dynamics ? 9 : 3) + 1;  /* skip pos + clock */
        idx_ref += (rtk->opt.tropopt < TROPOPT_EST) ? 0 :
                   (rtk->opt.tropopt >= TROPOPT_ESTG ? 3 : 1);  /* skip trop */
        idx_ref += MAXSAT * 0 + (ddamb[i].sat1 - 1);  /* ref sat ambiguity */

        idx_sat = (rtk->opt.dynamics ? 9 : 3) + 1;
        idx_sat += (rtk->opt.tropopt < TROPOPT_EST) ? 0 :
                   (rtk->opt.tropopt >= TROPOPT_ESTG ? 3 : 1);
        idx_sat += MAXSAT * 0 + (ddamb[i].sat2 - 1);  /* sat ambiguity */

        /* check validity */
        if (idx_ref >= nx || idx_sat >= nx) {
            trace(1, "apply_ar_fixed: invalid ambiguity index\n");
            continue;
        }

        /* DD = sat - ref */
        /* Design matrix: H[idx_sat] = 1, H[idx_ref] = -1 */
        H[idx_sat + nv * nx] = 1.0;
        H[idx_ref + nv * nx] = -1.0;

        /* Residual: v = (N_sat - N_ref) - DD_IF_fixed */
        /* But since we want: (N_sat - N_ref) = DD_IF_fixed */
        /* The pseudo-observation equation is: */
        /* 0 = (N_sat - N_ref) - DD_IF_fixed + v */
        /* So: v = DD_IF_fixed - (N_sat - N_ref) */
        v_pseudo = ddamb[i].DD_IF_fix - (rtk->x[idx_sat] - rtk->x[idx_ref]);
        v[nv] = v_pseudo;

        /* Measurement variance: very small to act as strong constraint */
        /* sigma = 0.001 cycles * lambda1 ~ 0.0002 m */
        /* variance = 0.0002^2 = 4e-8 m^2 */
        sigma_pseudo = 0.0002;  /* meters */
        R[nv + nv * count] = sigma_pseudo * sigma_pseudo;

        satno2id(ddamb[i].sat1, satid1);
        satno2id(ddamb[i].sat2, satid2);
        trace(3, "Pseudo-obs %d: DD %s-%s, N_sat=%.3f N_ref=%.3f, DD_fix=%.3f, v=%.3f\n",
              nv, satid2, satid1,
              rtk->x[idx_sat], rtk->x[idx_ref], ddamb[i].DD_IF_fix, v_pseudo);

        nv++;
    }

    if (nv > 0) {
        /* apply constraints using filter update */
        trace(2, "apply_ar_fixed: applying %d pseudo-observations\n", nv);

        /* call filter with pseudo-observations */
        /* filter(x, P, H, v, R, n, m) updates state vector x and covariance P */
        if (!filter(rtk->x, rtk->P, H, v, R, nx, nv)) {
            trace(1, "apply_ar_fixed: filter update failed\n");
            free(H); free(v); free(R);
            return 0;
        }

        /* copy to fixed solution */
        matcpy(rtk->xa, rtk->x, rtk->nx, 1);
        matcpy(rtk->Pa, rtk->P, rtk->nx, rtk->nx);
        rtk->na = rtk->nx;

        /* set solution status to FIXED */
        rtk->sol.stat = SOLQ_FIX;

        trace(2, "apply_ar_fixed: %d constraints applied, solution status=FIX\n", nv);
    }

    free(H);
    free(v);
    free(R);

    return nv;
}
