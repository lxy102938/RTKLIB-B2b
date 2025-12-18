# Pass-by-Pass AR 集成指南

## 配置文件修改

在现有的conf文件（如`postppp_25066_ulab.conf`）中添加以下配置项：

```conf
# Pass-by-Pass Ambiguity Resolution Settings
prcopt.armode_pbp     =3    # 0:off, 1:WL only, 2:WL+NL, 3:full AR with pseudo-obs
prcopt.pbp_refsat     =0    # reference satellite (0:auto select highest elevation)
prcopt.pbp_thresar_wl =0.15 # WL fixing threshold (cycles)
prcopt.pbp_thresar_nl =0.15 # NL fixing threshold (cycles)
prcopt.pbp_sigma_pseudo=0.0002 # pseudo-observation sigma (m), default 0.001 cycles

# 注意：必须使用两天的数据文件
# 第一天：infile0, infile1, infile3 (obs, nav, B2b)
# 第二天：infile1（第二天的nav）, infile4（第二天的B2b）
```

## 完整配置示例

```conf
# PPP-B2b 48h Processing with Pass-by-Pass AR
PPP_Glo.RT_flag   =0    #0:POST 1:Real-time

# prcopt_t prcopt
prcopt.mode       =7    # 7:ppp-kine, 8:ppp-static
prcopt.navsys     =33   # (32:BDS only, 33:GPS+BDS recommended)
prcopt.sateph     =5    # 5:brdc+PPP-B2b (必须)
prcopt.nf         =2    # 2:L1+L2 (必须，AR需要双频)
prcopt.elmin      =3    # (deg)
prcopt.ionoopt    =3    # 3:dual-freq (必须，无电离层组合)
prcopt.tropopt    =3    # 3:est-ztd
prcopt.tidecorr   =3    # 3:solid+otl+pole tide

# Pass-by-Pass AR Settings (新增部分)
prcopt.armode_pbp     =3    # 3:完整AR固定
prcopt.pbp_refsat     =0    # 0:自动选择参考星
prcopt.pbp_thresar_wl =0.15 # WL固定阈值
prcopt.pbp_thresar_nl =0.15 # NL固定阈值
prcopt.pbp_sigma_pseudo=0.0002 # 伪观测sigma

# 输入文件 - 48小时数据
# 第一天
infile0 = /path/to/obs/station0660.25h    # 第一天观测文件
infile1 = /path/to/nav/brd40660.25p       # 第一天广播星历
infile2 = /path/to/nav/brd40670.25p       # 第二天广播星历
infile3 = /path/to/B2b/2025066.B2b        # 第一天B2b
infile4 = /path/to/B2b/2025067.B2b        # 第二天B2b (关键!)
prcopt.B2b_format = 21  # 21:unicore

# 输出文件
outfile = ./out/station_AR_fixed.pos

# Solution options
solopt.posf       =0        # 0:llh
solopt.outhead    =1        # 1:输出文件头
solopt.outstat    =2        # 2:residual (显示详细信息)
solopt.trace      =3        # 3:显示AR过程 (debug用)
```

## 使用步骤

### 1. 准备数据
确保有完整的48小时(两天)数据：
- 连续的观测文件
- 两天的广播星历
- 两天的B2b改正文件

### 2. 运行处理
```bash
cd /path/to/RTKLIB-B2b/bin
./postppp -k path/to/your_config.conf
```

### 3. 查看输出
程序会输出以下信息：
```
Day 1 processing completed
Day 2 processing completed
2 days complete

========== Pass-by-Pass Ambiguity Resolution ==========
Collecting ambiguities from Day 1...
Collecting ambiguities from Day 2...
Total arcs: XX

Computing DD ambiguities with reference satellite GXX...
DD ambiguities computed: XX

Fixing WL ambiguities...
WL fixed: XX/XX

Fixing NL ambiguities...
NL fixed: XX/XX

Applying AR constraints with pseudo-observations...
AR fixed solution applied: XX constraints

=======================================================
```

### 4. 结果说明
- 浮点解(FLOAT): 处理过程中的所有历元
- 固定解(FIX): 应用AR约束后第二天的解
- 输出文件中 Q=1 表示固定解，Q=5 表示浮点解

## 参数调节

### 固定阈值
```conf
prcopt.pbp_thresar_wl =0.15  # 默认0.15周，可调节范围0.10-0.25
prcopt.pbp_thresar_nl =0.15  # 默认0.15周，可调节范围0.10-0.25
```
- 阈值越小：固定率越低，但固定的更可靠
- 阈值越大：固定率越高，但可能有错误固定

### 伪观测权重
```conf
prcopt.pbp_sigma_pseudo=0.0002  # 默认0.0002m (0.001周)
```
- 越小的sigma意味着越强的约束
- 建议范围: 0.0001-0.0005m

### 参考星选择
```conf
prcopt.pbp_refsat=0   # 0:自动选择（推荐）
prcopt.pbp_refsat=11  # 指定PRN（如G11）
```

## 故障排查

### 问题1: "No B2b files available"
- 检查B2b文件路径是否正确
- 确保infile3和infile4都指定了B2b文件

### 问题2: "No valid DD WL"
- 检查是否有足够的卫星观测(至少4颗)
- 确保两天数据有重叠的卫星

### 问题3: "WL fixed 0/XX"
- 检查观测质量(SNR, 多路径等)
- 尝试降低固定阈值
- 检查卫星高度角设置

### 问题4: 输出仍然是FLOAT
- 检查armode_pbp是否设置为3
- 查看trace输出确认AR流程是否执行
- 确保有足够的固定模糊度(至少2-3对DD)

## 进阶使用

### 只输出第二天固定解
修改代码在输出时添加时间过滤，或使用后处理工具提取：
```bash
# 提取第二天的数据
awk '$1 >= 2025/03/08 && $1 <= 2025/03/09' station_AR_fixed.pos > day2_only.pos
```

### 批处理多站点
```bash
#!/bin/bash
for conf in conf/*.conf; do
    echo "Processing $conf..."
    ./postppp -k $conf
done
```

## 理论背景

Pass-by-Pass AR方法通过以下步骤实现模糊度固定：

1. **48h浮点解**: 连续处理两天数据，获得IF模糊度估计
2. **WL固定**:
   - 使用HMW组合计算宽巷模糊度
   - 跨天差分消除卫星端偏差
   - 星间单差形成DD WL
   - 将DD WL固定为整数
3. **NL固定**:
   - 从DD IF和固定的DD WL反推DD NL
   - 将DD NL固定为整数
4. **AR固定解**:
   - 从固定的DD WL和DD NL重构DD IF
   - 构造伪观测约束模糊度
   - 重新解算获得固定解

## 参考文献

Geng J., Meng X., Dodson A.H., Teferle F.N. (2010). Integer ambiguity resolution in
precise point positioning: method comparison. Journal of Geodesy, 84(9), 569-581.

## 技术支持

如有问题，请提供：
1. 完整的配置文件
2. trace输出文件
3. 观测文件的时间范围和卫星数量

