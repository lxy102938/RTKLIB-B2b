/*------------------------------------------------------------------------------
* ppp_ar_passbypass.h : PPP ambiguity resolution using pass-by-pass method
*-----------------------------------------------------------------------------*/
#ifndef PPP_AR_PASSBYPASS_H
#define PPP_AR_PASSBYPASS_H

#include "rtklib.h"

#define MAXARC  200        /* max number of arcs per satellite */

/* ambiguity arc type */
typedef struct {
    gtime_t ts,te;     /* arc start/end time */
    int sat;           /* satellite number */
    int day;           /* day number (0=day1, 1=day2) */
    double N_IF;       /* IF ambiguity (m) */
    double var_IF;     /* IF ambiguity variance (m^2) */
    double N_WL;       /* WL ambiguity (cycles) */
    double var_WL;     /* WL ambiguity variance (cycles^2) */
    int nobs;          /* number of observations */
    int fixed_WL;      /* WL fix flag (0=float, 1=fixed) */
    int fixed_NL;      /* NL fix flag (0=float, 1=fixed) */
    double N_WL_fix;   /* fixed WL ambiguity (cycles) */
    double N_NL_fix;   /* fixed NL ambiguity (cycles) */
} ambarc_t;

/* satellite ambiguity arcs */
typedef struct {
    int n;             /* number of arcs */
    ambarc_t arc[MAXARC]; /* arc data */
} satamb_t;

/* double-difference ambiguity */
typedef struct {
    int sat1,sat2;     /* satellite pair */
    int arc1,arc2;     /* arc indices for sat1, sat2 */
    double DD_IF;      /* DD IF ambiguity (m) */
    double DD_WL;      /* DD WL ambiguity (cycles) */
    double DD_NL;      /* DD NL ambiguity (cycles) */
    double var_DD_IF;  /* DD IF variance */
    double var_DD_WL;  /* DD WL variance */
    double var_DD_NL;  /* DD NL variance */
    int fixed_WL;      /* WL fix flag */
    int fixed_NL;      /* NL fix flag */
    double DD_WL_fix;  /* fixed DD WL (integer cycles) */
    double DD_NL_fix;  /* fixed DD NL (integer cycles) */
    double DD_IF_fix;  /* fixed DD IF from WL&NL (m) */
} ddamb_t;

/* function prototypes */
extern double hmw_combination(double L1, double L2, double P1, double P2);
extern double nl_from_if_wl(double N_IF, double N_WL);
extern double if_from_wl_nl(double N_WL, double N_NL);
extern int fix_ambiguity(double N_float, double std, double threshold, int *fixed);
extern int ratio_test(const double *cand, int n, double threshold);
extern void init_arc_data(void);
extern void print_arc_summary(void);

/* collect ambiguities from RTK solution */
extern int collect_ambiguities(const rtk_t *rtk, const obsd_t *obs, int n,
                                 int day, satamb_t *satamb);

/* compute double-difference ambiguities */
extern int compute_dd_ambiguities(const satamb_t *satamb, int refsat,
                                   ddamb_t *ddamb, int *n_dd);

/* fix WL and NL ambiguities */
extern int fix_wl_nl_ambiguities(ddamb_t *ddamb, int n_dd);

/* apply AR fixed solution with pseudo-observations */
extern int apply_ar_fixed(rtk_t *rtk, const ddamb_t *ddamb, int n_dd);

/* ========== Integration Functions ========== */

/* AR processing wrapper for 48h PPP-B2b */
extern int ppp_ar_48h(const prcopt_t *popt, rtk_t *rtk, const obs_t *obs);

/* collect ambiguities during epoch processing */
extern int collect_ambiguities_epoch(const rtk_t *rtk, const obsd_t *obs, int n, int day);

#endif /* PPP_AR_PASSBYPASS_H */
