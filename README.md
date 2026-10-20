# M5Stack Stopwatch 展会电子胸牌

面向 M5Stack Stopwatch（C152，466×466 CO5300 圆形 AMOLED）的 ESP-IDF 5.5 胸牌固件。挂在胸前显示名片，按黄键切到全屏二维码给别人扫。

## 获取与编译

```bash
git clone --recursive <repo>          # 已经 clone 过：git submodule update --init --recursive
source $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

M5GFX、M5PM1、M5IOE1、ic_rx8130 都是 submodule，版本与 [M5StopWatch-PowerDemo](Reference/M5StopWatch-PowerDemo) 一致（M5GFX 0.2.28 `d91077b`）。编译时会自动把 `web/` 打包进 `include/web_assets.h`。

## 填写自己的资料

默认资料（第一次开机或恢复默认后显示的内容）来自 profile 目录，不写在代码里：

```bash
cp -r profile.example profile   # profile/ 已在 .gitignore 中，不会被提交
```

- `profile/profile.json`：姓名、职位、公司、标签、邮箱、电话、主题（`ocean` / `brand` / `paper`）、亮度、待机时间、扫码页停留时间和最多 3 个二维码。
- `profile/avatar.jpg`：方形 JPEG 头像。
- 二维码的 `text` 是二维码里的内容。微信二维码截图可以用 `python3 tools/decode_qr.py 截图.jpg` 读出来（需要 `pip install opencv-python`），也可以在网页编辑器里选择截图自动识别。

没有 `profile/` 时自动使用 `profile.example/`。修改后直接 `idf.py build` 即可生效。设备上已经保存过的配置不受影响，要让新默认值生效请在网页里点「恢复默认设置」。

## 使用

1. 首次开机显示设置页：`WIFI:` 二维码、热点名和随机密码。用手机系统相机扫码连接（微信扫一扫不支持 Wi-Fi 码），手机会自动打开编辑器；没打开就用浏览器访问 `192.168.4.1`。
2. 在编辑器里修改资料、头像、二维码、主题、亮度和超时，点「发布到胸牌」。
3. 发布后胸牌显示「已保存」，随后回到名片页并关闭热点。再次修改时长按蓝键 2 秒进入设置模式；5 分钟无操作自动退出。

| 操作 | 效果 |
| --- | --- |
| 黄键（G2）短按 | 打开扫码页，亮度拉满；到时或再按返回。停留时间在编辑器里设，10–30 秒，默认 30 秒 |
| 蓝键（G1）短按 | 扫码页有多个二维码时切换；只有一个时返回 |
| 蓝键长按 2 秒 | 进入 / 退出设置模式（开 / 关热点） |
| 无操作 | 低亮度待机页（时间 + 名字）。等待时间在编辑器里设，默认 45 秒，也可设为从不。任意键回到名片，黄键直接打开扫码 |
| 电源键短按 | 开机 |
| 电源键连按两次 | 关机。关机保持 L1，RTC 继续走时 |

充电电流为 425 mA（开机时把 PMG3 / `CHG_PROG` 拉低）。

## 续航

电池图标旁的 `xx.xh` 是剩余时间。放电时按模型电流累计已用电量，剩余时间 =（容量 − 已用）÷ 最近 15 分钟的平均电流。充电时不显示。

容量出厂 450 mAh。从满电放到约 3.50 V 时，用这次的累计电量改容量（限制在 280–520 mAh），并记下电压曲线。设置页「耗电」显示最近 12 小时的剩余时间和电压（约 5 分钟一个点），以及出厂曲线和本机曲线。「清除校准」回到 450 mAh 和出厂曲线，用电记录保留。

电流模型取自 PowerDemo 在 3.7 V 下的实测值。扫码页的高亮按亮度和画面发光量计入；马达按实际导通时间另记。

| 项 | 取值 | 来源 |
| --- | --- | --- |
| 基础电流（CPU 240 MHz、屏幕开、触摸/IMU/RTC 在线） | 44.0 mA | `normal_active` 45.69 mA，扣除其黑底界面的少量发光电流 |
| 屏幕发光，满亮度全白 | 110 mA | `display_only` 154.45 mA 减基础电流 |
| Wi-Fi 热点 | 69.4 mA | `wifi_connected` 105.73 mA 减 `imu_sampling` 36.32 mA |
| 马达 | 90 mA × 实际导通时间 | PowerDemo 没有单独的马达项；`full_load` 285.77 mA 把马达、喇叭和屏幕绑在一起。90 mA 是补记余量 |

按这个模型：深色名片页约 48 mA，待机页约 45 mA，扫码页约 84 mA，设置模式（热点开）约 135 mA。

充电记账按（425 mA − 当前负载）× 0.7 把电记回电池。电压在 4.12 V 以上保持 1 分钟，或者本次已经在累计、拔出时仍不低于 4.10 V，就从满电重新计。开机时如果电压和关机前接近，就接着上次的累计；否则用本机曲线估计起点，还没有曲线时用出厂电压表。

## 架构

- **卡面由浏览器渲染**：编辑器用手机系统字体把名片页和待机名字条画成 JPEG 图层上传到 SPIFFS，预览和设备用同一套绘制代码（`web/compose.js`）。
- **动态内容由固件绘制**：二维码（ECC M，整数像素模块，4 格静区，绘制后回读校验）、时间、电量、倒计时点环、设置页。
- **显示驱动**：`Panel_CO5300` + PowerDemo 的初始化序列与复位时序，整帧在一次事务内刷新，亮度命令等上一帧 DMA 完成后再发。
- **热点按需开启**：只在设置页打开 SoftAP，每次生成新密码；DHCP option 114 和常见探测地址重定向让手机自动打开编辑器。
- **防烧屏**：名片页每分钟整体偏移 3px，待机页偏移 8px。

| 文件 | 作用 |
| --- | --- |
| `main/ui_router.*` | 页面状态机、转场、超时、亮度曲线（不依赖硬件，模拟器共用） |
| `main/ui_pages.*`、`ui_theme.h` | 名片、扫码、待机、设置页的绘制与设计参数 |
| `main/display.*` | CO5300 面板与帧缓冲 |
| `main/wifi_portal.*`、`dns_server.*` | 按需热点、HTTP API、captive portal |
| `main/input.*`、`haptics.*`、`power.*`、`rtc_time.*` | 按键、振动、电量与续航、RX8130 时间 |
| `main/badge_config.*` | NVS 配置与 JSON 互转，默认值来自 profile |
| `main/board.*` | I2C、PM1（L1 保持、快充）、IO 扩展 |
| `tools/gen_profile.py` | 把 profile 生成 `badge_profile.h` 和默认头像 |
| `web/` | 编辑器；`vendor/` 内是 jsQR（Apache-2.0）和 qrcode-generator（MIT） |

HTTP API：`GET /api/state`、`GET /api/power`、`POST /api/power/reset`（清除校准）、`POST /api/config`（JSON）、`POST /api/time`、`POST /api/assets`（`X-Asset-Name: card|about|name|avatar`，JPEG，先写 `.tmp` 再替换）、`POST /api/reset`、`GET /avatar.jpg`。

## 在电脑上预览

```bash
# 编辑器 + 模拟 API（默认资料同样来自 profile），发布的图层写到 .cache/dev_assets
python3 tools/dev_server.py --port 8766

# 用固件同一份页面代码渲染所有页面，检查二维码回读和路由逻辑
cmake -S sim -B sim/build && cmake --build sim/build -j
sim/build/badge_sim --assets .cache/dev_assets --out .cache/sim --theme 0
```
