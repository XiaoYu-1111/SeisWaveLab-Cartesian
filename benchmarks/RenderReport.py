"""Render the completed production-solver suite; never substitutes synthetic timings."""
from pathlib import Path
import csv, json, hashlib, shutil, statistics, argparse, math
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
import numpy as np

parser=argparse.ArgumentParser()
parser.add_argument('--results',required=True,type=Path)
parser.add_argument('--publish',required=True,type=Path)
a=parser.parse_args();base=a.results.resolve();publish=a.publish.resolve()
report=base/'report';assets=report/'assets';data=report/'data';assets.mkdir(parents=True,exist_ok=True);data.mkdir(exist_ok=True)
all_summary=[];telemetry=[];hardware={};trial_count=0
for dimension in (2,3):
    source=base/f'formal-{dimension}d'
    assert (source/'COMPLETE.txt').exists(),f'{source} is incomplete'
    hardware[dimension]=json.loads((source/'hardware.json').read_text(encoding='utf-8-sig'))
    assert hardware[dimension]['actual_threads']==32 and hardware[dimension]['requested_threads']==32
    with (source/'summary.csv').open(encoding='utf-8-sig',newline='') as f:
        rows=list(csv.DictReader(f))
    assert len(rows)==9
    for row in rows:
        row={k:float(v) if k!='backend' else v for k,v in row.items()}
        assert row['relative_l2']<2e-4
        all_summary.append(row)
    with (source/'trials.csv').open(encoding='utf-8-sig',newline='') as f:
        trials=list(csv.DictReader(f));trial_count+=len(trials)
    assert len(trials)==45
    for row in rows:
        matching=[float(t['ms_per_step']) for t in trials if t['size']==row['size'] and t['backend']==row['backend'] and t['sync_batch']==row['sync_batch']]
        assert len(matching)==5
        assert math.isclose(statistics.median(matching),float(row['median_ms_step']),rel_tol=1e-9)
    for name in ('summary.csv','trials.csv','hardware.json','gpu-telemetry.csv','run.log'):
        shutil.copy2(source/name,data/f'{dimension}d-{name}')
    with (source/'gpu-telemetry.csv').open(encoding='utf-8-sig',newline='') as f:
        for row in csv.DictReader(f): telemetry.append({k.strip():v.strip() for k,v in row.items()})

def get(dim,size,backend,batch=1):
    return next(r for r in all_summary if int(r['dimension'])==dim and int(r['size'])==size and r['backend']==backend and int(r['sync_batch'])==batch)

sizes={2:[256,1024,2048],3:[48,96,160]};cases=[]
for dim in (2,3):
    for size in sizes[dim]:
        c,g,b=get(dim,size,'CPU'),get(dim,size,'CUDA'),get(dim,size,'CUDA',8)
        cases.append(dict(dimension=dim,size=size,physical_nodes=int(c['physical_nodes']),padded_nodes=int(c['padded_nodes']),cpu32_ms=c['median_ms_step'],cuda_sync1_ms=g['median_ms_step'],cuda_sync8_ms=b['median_ms_step'],speedup_sync1=g['cpu_over_cuda'],speedup_sync8=b['cpu_over_cuda'],cpu32_physical_mnodes_s=c['physical_mnodes_per_second'],cuda_sync8_physical_mnodes_s=b['physical_mnodes_per_second'],relative_l2=c['relative_l2']))
with (data/'comparison.csv').open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(cases[0]));writer.writeheader();writer.writerows(cases)
shutil.copy2(base/'power-plan.txt',data/'power-plan.txt')

font=Path('C:/Windows/Fonts/msyh.ttc')
if font.exists(): plt.rcParams['font.family']=FontProperties(fname=str(font)).get_name()
plt.rcParams.update({'axes.unicode_minus':False,'font.size':11,'axes.spines.top':False,'axes.spines.right':False,'figure.facecolor':'#fcfaf5','axes.facecolor':'#fcfaf5','axes.edgecolor':'#b9b9ac','grid.color':'#dddcd4','savefig.facecolor':'#fcfaf5'})
colors={'cpu':'#285e72','sync1':'#d87b49','sync8':'#219689'}
fig,axes=plt.subplots(1,2,figsize=(13.8,5.6))
for ax,dim in zip(axes,(2,3)):
    x=np.arange(3);cpu=[get(dim,s,'CPU')['median_ms_step'] for s in sizes[dim]];gpu=[get(dim,s,'CUDA')['median_ms_step'] for s in sizes[dim]];batch=[get(dim,s,'CUDA',8)['median_ms_step'] for s in sizes[dim]]
    for values,color,label,style in [(cpu,colors['cpu'],'CPU / OpenMP · 32 线程','-'),(gpu,colors['sync1'],'CUDA · 每步同步','--'),(batch,colors['sync8'],'CUDA · 每 8 步同步','-')]:
        ax.plot(x,values,style,marker='o',markersize=6,lw=2.1,color=color,label=label)
    for index,values in enumerate((cpu,batch)):
        for xx,v in zip(x,values): ax.annotate(f'{v:.3f}',(xx,v),xytext=(0,9 if index==0 else -20),textcoords='offset points',ha='center',color=colors['cpu' if index==0 else 'sync8'],fontsize=10)
    ax.set_yscale('log');ax.set_xticks(x,[f'{s}'+('²' if dim==2 else '³') for s in sizes[dim]])
    ax.set_xlim(-.2,2.2);ax.grid(True,axis='y',which='major',lw=.7);ax.set_title(f'{dim}D 生产求解器',loc='left',fontweight='bold',pad=18);ax.set_xlabel('物理模型尺寸');ax.set_ylabel('毫秒 / 步（对数坐标，越低越快）');ax.legend(loc='upper left',fontsize=9,frameon=False)
    ax.margins(y=.34)
fig.suptitle('CPU / CUDA 计算性能 · 32 线程 · 5 轮中位数',fontsize=17,fontweight='bold',x=.065,ha='left')
fig.text(.065,.015,'i9-13900HX + RTX 4060 Laptop 8GB  |  Float32  |  纯求解，不含显示、采集和文件 I/O  |  2026-10-19',fontsize=9,color='#62695e')
fig.tight_layout(rect=(.025,.07,.995,.92));fig.savefig(assets/'compute-time.png',dpi=190);fig.savefig(assets/'compute-time.svg');plt.close(fig)
fig,ax=plt.subplots(figsize=(12.5,5.3));x=np.arange(6);width=.34
for shift,key,color,label in [(-width/2,'speedup_sync1',colors['sync1'],'CUDA 每步同步'),(width/2,'speedup_sync8',colors['sync8'],'CUDA 每 8 步同步')]:
    values=[r[key] for r in cases];bars=ax.bar(x+shift,values,width,color=color,label=label)
    ax.bar_label(bars,labels=[f'{v:.1f}×' for v in values],padding=4,fontsize=10)
ax.axvline(2.5,color='#bdbdb3',lw=1);ax.set_xticks(x,[f"{c['dimension']}D\n{c['size']}"+('²' if c['dimension']==2 else '³') for c in cases]);ax.set_ylabel('CPU 32 线程耗时 ÷ CUDA 耗时');ax.set_ylim(0,max(c['speedup_sync8'] for c in cases)*1.22);ax.set_title('CUDA 相对 CPU 32 线程的加速比',loc='left',fontweight='bold',fontsize=16,pad=18);ax.legend(frameon=False);ax.grid(axis='y',lw=.7);ax.set_axisbelow(True);fig.tight_layout();fig.savefig(assets/'speedup.png',dpi=190);plt.close(fig)

max_temp=max(float(r['temperature.gpu']) for r in telemetry);max_clock=max(float(r['clocks.current.sm [MHz]'].split()[0]) for r in telemetry);max_power=max(float(r['power.draw [W]'].split()[0]) for r in telemetry)
max_error=max(c['relative_l2'] for c in cases)
h=hardware[2]
ranges={dim:(min(c['speedup_sync8'] for c in cases if c['dimension']==dim),max(c['speedup_sync8'] for c in cases if c['dimension']==dim)) for dim in (2,3)}
lines=['# 二维 / 三维 CPU 与 CUDA 性能测试报告','', '测试日期：2026-10-19（Asia/Shanghai）。本机生产求解器实测；CPU 固定 **32 工作线程**。', '',
f'六档模型均完成 5 轮，全部 CPU/CUDA Vz 波场对照通过。以 CUDA 每 8 步同步计算，二维加速 **{ranges[2][0]:.2f}–{ranges[2][1]:.2f} 倍**，三维加速 **{ranges[3][0]:.2f}–{ranges[3][1]:.2f} 倍**。这些倍数比较的是本项目当前 CPU 实现，不表示 CPU 与 GPU 硬件理论峰值差。','',
'## 1. 测试环境','',f"- CPU：{h['cpu']}，{h['physical_cores']} 物理核 / {h['logical_processors']} 逻辑处理器；请求与实际 OpenMP 工作线程均为 {h['actual_threads']}。",
f"- CPU 亲和性掩码：`0x{h['process_affinity_mask']:X}`，覆盖全部 32 个逻辑处理器；关闭 OpenMP 动态缩减，测试进程 Above Normal 优先级。",
f"- GPU：{h['gpu']}，{h['gpu_memory_bytes']/1024**3:.2f} GiB，Compute Capability {h['compute_capability']}；驱动 {telemetry[0]['driver_version']}，CUDA Runtime 12.8。",
f"- 内存：{h['ram_bytes']/1024**3:.2f} GiB；接电运行。",
'- 系统当前仅列出“平衡”电源方案；保持原方案，没有更改 BIOS、超频、GPU 功率上限或系统电源策略。因此这是当前机器环境下的 32 线程结果，不能宣称已测得硬件绝对峰值。',
f'- GPU 预热期间采样到 P0；整轮最高温度 {max_temp:.0f}°C、SM 频率 {max_clock:.0f} MHz、功耗 {max_power:.2f} W。遥测约每 2 秒一次，不能代表瞬时峰值或每次计时段的精确状态。',
'- 构建：外层 `out/build/x64-Release`，`RelWithDebInfo`，MSVC 19.44、C++20、`/O2`。二维 CUDA 沿用程序的 `--use_fast_math`，三维 CUDA 沿用其独立编译选项；CPU/CUDA 均为 Float32。','',
'## 2. 模型与计时协议','',
'| 项目 | 二维 | 三维 |','|---|---|---|','| 小 / 中 / 稍大模型 | 256² / 1024² / 2048² | 48³ / 96³ / 160³ |','| 求解方案 | Type 3：自由表面 + 外置分裂 PML | 平面自由表面 + CPML |','| 吸收层宽度 | 16 格 | 12 格 |','| 每轮预热 / 计时 | 20 / 100 步 | 20 / 40 步 |','| 每模式重复 | 5 轮，报告中位数与四分位范围 | 5 轮，报告中位数与四分位范围 |','',
'每轮包含预热后的总物理时长为二维 60 ms、三维 30 ms；正式计时窗口分别为 10–60 ms 与 10–30 ms，并非完整长时地震记录。', '',
'采用双层弹性介质，上层 Vp/Vs/ρ 为 3000 m/s、1700 m/s、2200 kg/m³，下层为 4200 m/s、2400 m/s、2500 kg/m³；界面位于半深度。两维度均使用 dx=10 m、dt=0.5 ms、Ricker 30 Hz、垂向集中力源，震源位于水平中心与四分之一深度。各 CPU/CUDA 配对使用完全相同的模型、震源和步数。源实现及其幅值约定分别沿用各维度生产接口，不能将二维和三维视为同等每节点工作量。','',
'时间来自 `steady_clock` 墙钟，包括应力、速度、震源、吸收边界及 CPML 更新，以及主机提交和必要的 CUDA 完成同步。计时排除初始化、分配、清场、显示、接收器采样、文件导出及误差验证下载。CUDA 测两种模式：每步完成同步、每 8 步完成同步；不足 8 步的尾批也等待完成。CPU 与 CUDA 顺序运行，避免两个求解器同时争用资源。','',
'GPU 每轮先等待 600 ms，使上一阶段 CPU 线程退出活跃等待，然后用生产求解器预运行至少 300 ms，清场后再执行与 CPU 一致的 20 步预热；预运行与等待不进入统计。CPU 每轮从同样的零场开始。','',
'## 3. 核心结果','',
'单位：ms/步，越低越快。“加速比”=32 线程 CPU 中位耗时÷CUDA 中位耗时。','',
'| 维度 / 档位 | 物理尺寸 | CPU 32线程 | CUDA 每步同步 | CUDA 每8步同步 | 每步加速比 | 批量加速比 |','|---|---:|---:|---:|---:|---:|---:|']
for i,c in enumerate(cases):
    label=['小','中','稍大'][i%3];dimension=c['dimension'];size=c['size'];shape=' × '.join([str(size)]*dimension)
    lines.append(f"| {dimension}D {label} | {shape} | {c['cpu32_ms']:.3f} | {c['cuda_sync1_ms']:.3f} | {c['cuda_sync8_ms']:.3f} | {c['speedup_sync1']:.2f}× | {c['speedup_sync8']:.2f}× |")
lines+=['','![耗时对比](assets/compute-time.png)','','![加速比](assets/speedup.png)','','## 4. 吞吐量、域尺寸和数值一致性','','吞吐量用**物理节点数×推进步数/秒**归一化，包含计算吸收层所消耗的时间；它不是浮点运算次数。分配域含吸收层与守卫节点，最外层守卫并不都执行同等内点更新。','','| 维度 / 尺寸 | 物理节点 | 分配域节点 | CPU 32线程（百万物理节点·步/s） | CUDA批8步（百万物理节点·步/s） | Vz相对L2误差 |','|---|---:|---:|---:|---:|---:|']
for c in cases: lines.append(f"| {c['dimension']}D {c['size']} | {c['physical_nodes']:,} | {c['padded_nodes']:,} | {c['cpu32_physical_mnodes_s']:.2f} | {c['cuda_sync8_physical_mnodes_s']:.2f} | {c['relative_l2']:.3e} |")
lines+=['',f'误差采用全部物理节点的 Vz，`||Vz_CPU−Vz_CUDA||₂ / ||Vz_CPU||₂`，最大为 **{max_error:.3e}**，全部低于预设 `2e-4`。两种 CUDA 同步方式的波场也一致（检查阈值 `1e-6`）。未使用随机值、假波形或估计耗时。','',
'## 5. 重复性：P25–P75','','以下为 5 轮每步耗时的四分位范围；小模型计时短，动态频率和调度抖动更容易改变比值。','','| 维度 / 尺寸 | CPU 32线程 ms | CUDA逐步同步 ms | CUDA批8步 ms |','|---|---:|---:|---:|']
for c in cases:
    values=[]
    for backend,batch in [('CPU',1),('CUDA',1),('CUDA',8)]:
        row=get(c['dimension'],c['size'],backend,batch);values.append(f"{row['p25_ms_step']:.3f}–{row['p75_ms_step']:.3f}")
    lines.append(f"| {c['dimension']}D {c['size']} | "+' | '.join(values)+' |')
lines+=['','## 6. 对当前软件的含义','',
'- 六档模型均适合优先使用 CUDA；本次 CUDA 都快于固定 32 线程 CPU。CPU 保留作为无可用 CUDA 时的计算回退。',
'- 32 线程不保证每个小模型都比少线程更快。本轮按要求固定 32 线程，没有测线程数扫描，因此不能据此断言 32 是各档模型的最优线程数。',
'- 当前二维界面逐步同步，对应主表“CUDA 每步同步”；当前三维界面按最多 8 步一批推进，纯求解行为更接近批量列，但其计时还可能含接收器处理。批8步不是每帧固定8步的界面FPS。',
'- 小模型的 CPU/GPU 加速比更大，不能线性外推到任意大模型。吸收层相对开销、固定并行调度成本、缓存/访存等都可能参与；本轮没有硬件计数器剖析，不将其写成已证实的单一瓶颈。',
'- 批8步只在二维小模型出现约一成改善，中大模型改善很小或落在波动范围内。因此仅减少同步次数不足以解释或解决全部性能差异。',
'- 结果不包括实时体绘制、纹理提取、采集写盘和窗口刷新；完整工作区运行通常比这里报告的求解步率低。','',
'## 7. 可复现产物与运行方式','',
f'- 本次正式数据共 **{trial_count}** 个计时区块（6尺寸 × 3模式 × 5轮）；二维每模式每尺寸计时500步，三维计时200步。',
'- [六档汇总 CSV](data/comparison.csv)',
'- [二维逐轮数据](data/2d-trials.csv) · [三维逐轮数据](data/3d-trials.csv)',
'- [二维硬件/协议](data/2d-hardware.json) · [三维硬件/协议](data/3d-hardware.json)',
'- [二维GPU遥测](data/2d-gpu-telemetry.csv) · [三维GPU遥测](data/3d-gpu-telemetry.csv)',
'- [源文件与程序指纹](data/provenance.json)',
'- 基准入口：`benchmarks/ComputeBenchmark.cpp`，直接链接生产二维/三维计算代码；绘图入口：`benchmarks/RenderReport.py`。','',
'```powershell',
'cmake -S . -B out/build/x64-Release -DSEISWAVE_BUILD_BENCHMARKS=ON',
'cmake --build out/build/x64-Release --target SeisComputeBenchmark -j 4',
'& ./out/bin/RelWithDebInfo/SeisComputeBenchmark.exe --output out/benchmarks/retest/2d --threads 32 --repeats 5 --warmup 20 --steps 100 --dimension 2 --ftz 0',
'& ./out/bin/RelWithDebInfo/SeisComputeBenchmark.exe --output out/benchmarks/retest/3d --threads 32 --repeats 5 --warmup 20 --steps 40 --dimension 3 --ftz 0',
'```','',
'上述命令在外层工作区、已加载 VS x64 编译环境时执行；所有产物放外层 `out`。`--ftz 0` 为本报告采用的默认浮点环境对照模式；`--ftz 1` 仅供单独的次正规数处理实验，不属于主表。原模拟程序的计算设置和默认线程数没有因本次测试而改变。',
]
(report/'CPU_CUDA_REPORT.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
repo=Path(__file__).resolve().parent.parent
source_paths=['benchmarks/ComputeBenchmark.cpp','src/Seis2D/CpuSolver2D.cpp','src/Seis2D/SolverCells2D.h','src/Seis2D/cuda/Cuda_Check.cu','src/Seis3D/CpuSolver3D.cpp','src/Seis3D/SolverCells3D.h','src/Seis3D/cuda/Cuda_Seis3D.cu','CMakeLists.txt','benchmarks/RunSuite.ps1','benchmarks/RenderReport.py']
provenance={'note':'Working tree benchmark; source hashes describe the delivered reproducible harness. Optional FTZ experiment support was added after the main default-mode runs. Production solver source was unchanged during formal timings.','source_sha256':{name:hashlib.sha256((repo/name).read_bytes()).hexdigest() for name in source_paths},'benchmark_exe_sha256':hashlib.sha256((base.parents[1]/'bin/RelWithDebInfo/SeisComputeBenchmark.exe').read_bytes()).hexdigest()}
(data/'provenance.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
publish.mkdir(parents=True,exist_ok=True)
shutil.copytree(report,publish,dirs_exist_ok=True)
print(json.dumps({'report':str(publish/'CPU_CUDA_REPORT.md'),'cases':len(cases),'formal_trials':trial_count,'max_l2':max_error,'max_gpu_temp_c':max_temp,'max_gpu_sm_mhz':max_clock,'max_gpu_power_w':max_power,'results':cases},ensure_ascii=False,indent=2))
