# Pass-by-Pass AR 诊断指南

## 完整函数代码对照

### 1. postpos.c - procpos函数中的AR收集代码 (774-840行)

**位置**: 在 `if (!rtkpos(rtk, obs_ptr,n,&navs))` 之后

```c
/* Pass-by-Pass AR: collect ambiguities if enabled */
if (popt->armode_pbp > 0 && rtk->sol.stat == SOLQ_PPP) {
    static int day_transition_reported = 0;
    static int first_gps_doy = -1;  /* GPS day of year for first observation */

    /* Get current GPS day of year */
    double ep[6];
    time2epoch(obs_ptr[0].time, ep);
    int year = (int)ep[0];
    int month = (int)ep[1];
    int day_of_month = (int)ep[2];

    /* Calculate day of year */
    int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if ((year%4==0 && year%100!=0) || year%400==0) days_in_month[1] = 29;  /* leap year */
    int current_gps_doy = day_of_month;
    for (int m = 0; m < month - 1; m++) {
        current_gps_doy += days_in_month[m];
    }

    /* Initialize on first observation */
    if (day1_start.time == 0) {
        day1_start = obs_ptr[0].time;
        first_gps_doy = current_gps_doy;
        current_day = 0;
        char time_str_buf[64];
        time2str(day1_start, time_str_buf, 0);
        trace(1, "AR: Day 0 started at %s (GPS DOY %d)\n", time_str_buf, first_gps_doy);
        printf("AR: Day 0 started at %s (GPS DOY %d)\n", time_str_buf, first_gps_doy);
    }

    /* Calculate day number based on GPS DOY difference */
    int prev_day = current_day;
    current_day = current_gps_doy - first_gps_doy;

    /* Handle year rollover (DOY 365/366 -> 1) */
    if (current_day < 0) {
        current_day += ((year%4==0 && year%100!=0) || year%400==0) ? 366 : 365;
    }

    /* Report day transition */
    if (current_day == 1 && prev_day == 0 && !day_transition_reported) {
        char time_str_buf[64];
        time2str(obs_ptr[0].time, time_str_buf, 0);
        trace(1, "AR: Day 1 started at %s (GPS DOY %d)\n", time_str_buf, current_gps_doy);
        printf("AR: Day 1 started at %s (GPS DOY %d)\n", time_str_buf, current_gps_doy);
        day_transition_reported = 1;
    }

    /* Collect ambiguities for current epoch (only first two days) */
    if (current_day <= 1) {
        int n_collected = collect_ambiguities_epoch(rtk, obs_ptr, n, current_day);
        if (n_collected > 0) {
            trace(3, "AR: Collected %d ambiguities for day %d (DOY %d)\n",
                  n_collected, current_day, current_gps_doy);
        }
    } else if (current_day >= 2) {
        static int day2_warning_shown = 0;
        if (!day2_warning_shown) {
            trace(1, "AR: Day %d detected (DOY %d), stopping collection\n",
                  current_day, current_gps_doy);
            printf("AR: Day %d detected (DOY %d), stopping ambiguity collection\n",
                   current_day, current_gps_doy);
            day2_warning_shown = 1;
        }
    }
}
```

### 2. 必须的static变量声明 (postpos.c 728-729行)

**位置**: 在procpos函数开头，局部变量声明处

```c
static gtime_t day1_start = {0};  /* first day start time for AR */
static int current_day = -1;      /* current processing day for AR */
```

### 3. collect_ambiguities_epoch函数 (ppp_ar_integration.c)

```c
extern int collect_ambiguities_epoch(const rtk_t *rtk, const obsd_t *obs, int n, int day)
{
    extern satamb_t satamb[];
    return collect_ambiguities(rtk, obs, n, day, satamb);
}
```

### 4. print_arc_summary函数 (ppp_ar_passbypass.c 149-197行)

```c
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
```

## 诊断步骤

### 第1步：检查是否看到Day转换消息

运行后查找输出：
```
AR: Day 0 started at 2025/03/07 XX:XX:XX (GPS DOY 66)
```

**如果没看到这条消息**：
- 检查配置文件中 `prcopt.armode_pbp` 是否设置为非0值
- 检查是否有PPP解算成功 (rtk->sol.stat == SOLQ_PPP)

### 第2步：检查是否有Day 1转换

查找：
```
AR: Day 1 started at 2025/03/08 XX:XX:XX (GPS DOY 67)
```

**如果没看到这条消息**：
- 数据可能只有一天
- DOY计算可能有问题
- 两个B2b文件的日期可能相同

### 第3步：查看Arc分布

查找：
```
Arc distribution: day0=XXXX, day1=YYYY, other=0
```

**正常情况**: day0和day1都应该有数值
**异常情况**:
- `day0=10333, day1=0, other=0` → 所有数据都在day 0
- `day0=0, day1=10333, other=0` → 所有数据都在day 1
- `other>0` → day编号计算错误

### 第4步：如果仍然失败，添加额外诊断

在postpos.c的AR收集代码中添加调试输出：

```c
/* 在计算current_gps_doy之后添加 */
if (current_gps_doy != first_gps_doy) {
    static int last_reported_doy = -1;
    if (current_gps_doy != last_reported_doy) {
        printf("DEBUG: current_gps_doy=%d, first_gps_doy=%d, current_day=%d\n",
               current_gps_doy, first_gps_doy, current_day);
        last_reported_doy = current_gps_doy;
    }
}
```

## 可能的问题原因

### 原因1: 数据文件只有一天

检查你的观测文件(RINEX)和B2b文件：
- RINEX文件时间范围
- B2b文件命名 (应该有两个不同DOY的文件)

### 原因2: PPP解算不成功

如果 `rtk->sol.stat != SOLQ_PPP`，不会收集模糊度。
检查PPP解算质量。

### 原因3: DOY计算代码未生效

检查你的postpos.c是否真的包含了新的DOY计算代码。
重新编译后运行: `make clean && make -j4`

### 原因4: 静态变量初始化问题

如果procpos被多次调用，静态变量不会重置。
这是正常的，但如果你手动重启程序，应该会重置。

## 输出示例

### 正常输出应该是：

```
AR: Day 0 started at 2025/03/07 12:30:45 (GPS DOY 66)
...处理中...
AR: Day 1 started at 2025/03/08 00:00:00 (GPS DOY 67)
...处理中...
AR: Day 2 detected (DOY 68), stopping ambiguity collection
Day 2 processing completed

========== Pass-by-Pass Ambiguity Resolution ==========

========== Ambiguity Arc Summary ==========
Arc distribution: day0=5000, day1=5333, other=0
G01: 2 arcs [day0+day1]
  Arc 0: day=0, nobs=150, N_IF=...
  Arc 1: day=1, nobs=180, N_IF=...
G02: 2 arcs [day0+day1]
  Arc 0: day=0, nobs=120, N_IF=...
  Arc 1: day=1, nobs=200, N_IF=...
...
Total arcs: 10333
Satellites with both day0 and day1 (>=10 obs): 8

Reference satellite: G10
DD ambiguities computed: 7
Fixed ambiguities: 5/7
AR constraints applied: 5
Solution status: FIXED
```

### 异常输出（你现在的情况）：

```
AR: Day 0 started at 2025/03/07 12:30:45 (GPS DOY 66)
...没有Day 1消息...
Day 2 processing completed

========== Pass-by-Pass Ambiguity Resolution ==========

========== Ambiguity Arc Summary ==========
Arc distribution: day0=10333, day1=0, other=0
...
Total arcs: 10333
Satellites with both day0 and day1 (>=10 obs): 0
Error: No satellite has both day 0 and day 1 data (need >=10 obs each day)
```

## 下一步操作

1. **确认代码完全一致**：比对上面的函数代码
2. **重新完整编译**：`cd build && make clean && cmake .. && make -j4`
3. **运行并复制完整输出**：从"AR: Day 0 started"开始的所有输出
4. **提供数据文件信息**：
   - 观测文件时间范围
   - B2b文件列表和时间范围
   - infile3 和 infile4 的实际文件名

把这些信息发给我，我会帮你进一步诊断！
