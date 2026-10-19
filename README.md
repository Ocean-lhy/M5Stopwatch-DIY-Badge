# M5Stack Stopwatch 展会电子胸牌

面向 M5Stack Stopwatch（C152，466×466 CO5300 圆形 AMOLED）的 ESP-IDF 5.5 胸牌固件。挂在胸前显示名片，按一下黄键切到全屏二维码给别人扫码。

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

- `profile/profile.json`：姓名、职位、公司、介绍、标签、主题（`ocean` / `brand` / `paper`）和最多 3 个二维码。
- `profile/avatar.jpg`：方形 JPEG 头像。
- 二维码的 `text` 是二维码里的内容。微信二维码截图可以用 `python3 tools/decode_qr.py 截图.jpg` 读出来（需要 `pip install opencv-python`），也可以在网页编辑器里选择截图自动识别。

没有 `profile/` 时自动使用 `profile.example/`。修改后直接 `idf.py build` 即可生效。设备上已经保存过的配置不受影响，要让新默认值生效请在网页里点"恢复默认设置"。

## 使用

1. 首次开机显示设置页：`WIFI:` 二维码、热点名和随机密码。用手机系统相机扫码连接（微信扫一扫不支持 Wi-Fi 码），手机会自动打开编辑器；没打开就用浏览器访问 `192.168.4.1`。
2. 在编辑器里修改资料、头像、二维码和主题，点"发布到胸牌"。
3. 发布后胸牌显示"已保存"，随后回到名片页并关闭热点。再次修改时长按蓝键 2 秒进入设置模式；5 分钟无操作自动退出。

| 操作 | 效果 |
| --- | --- |
| 黄键（G2）短按 | 打开扫码页，亮度拉满，30 秒后自动返回；在扫码页再按返回 |
| 蓝键（G1）短按 | 名片页与介绍页切换；扫码页有多个二维码时切换 |
| 蓝键长按 2 秒 | 进入 / 退出设置模式（开 / 关热点） |
| 左右滑动 | 翻页；扫码页点按切换二维码 |
| 无操作 45 秒 | 进入低亮度待机页（时间 + 名字），任意键唤醒 |

## 续航估算

电池图标旁显示的 `xx.xh` 是按当前电量和模型电流推算的剩余时间（450 mAh × 电量 ÷ 平均电流，充电时不显示）。电流模型取自 PowerDemo 在 3.7 V 下的实测值：

| 项 | 取值 | 来源 |
| --- | --- | --- |
| 基础电流（CPU 240 MHz、屏幕开、触摸/IMU/RTC 在线） | 44.0 mA | `normal_active` 45.69 mA，扣除其黑底界面的少量发光电流 |
| 屏幕发光，满亮度全白 | 110 mA | `display_only` 154.45 mA 减基础电流 |
| Wi-Fi 热点 | 69.4 mA | `wifi_connected` 105.73 mA 减 `imu_sampling` 36.32 mA |

屏幕发光部分按"亮度 × 当前画面的平均发光量"计算，平均发光量由固件对每帧画面采样得到；最终电流取 15 分钟滑动平均，所以会反映名片页、待机页和扫码页的实际使用比例。按这个模型：深色名片页约 48 mA，待机页约 45 mA，扫码页约 84 mA，设置模式（热点开）约 135 mA。

## 架构

- **卡面由浏览器渲染**：编辑器用手机系统字体把名片页、介绍页和待机名字条画成 JPEG 图层上传到 SPIFFS，预览和设备用同一套绘制代码（`web/compose.js`）。
- **动态内容由固件绘制**：二维码（ECC M，整数像素模块，4 格静区，绘制后回读校验）、时间、电量、倒计时点环、设置页。
- **显示驱动**：`Panel_CO5300` + PowerDemo 的初始化序列与复位时序，整帧在一次事务内刷新，亮度命令等上一帧 DMA 完成后再发。
- **热点按需开启**：只在设置页打开 SoftAP，每次生成新密码；DHCP option 114 和常见探测地址重定向让手机自动打开编辑器。
- **防烧屏**：名片页每分钟整体偏移 3px，待机页偏移 8px。

| 文件 | 作用 |
| --- | --- |
| `main/ui_router.*` | 页面状态机、转场、超时、亮度曲线（不依赖硬件，模拟器共用） |
| `main/ui_pages.*`、`ui_theme.h` | 五个页面的绘制与设计参数 |
| `main/display.*` | CO5300 面板与帧缓冲 |
| `main/wifi_portal.*`、`dns_server.*` | 按需热点、HTTP API、captive portal |
| `main/input.*`、`haptics.*`、`power.*`、`rtc_time.*` | 按键/触摸、振动、电量与续航模型、RX8130 时间 |
| `main/badge_config.*` | NVS 配置与 JSON 互转，默认值来自 profile |
| `tools/gen_profile.py` | 把 profile 生成 `badge_profile.h` 和默认头像 |
| `web/` | 编辑器；`vendor/` 内是 jsQR（Apache-2.0）和 qrcode-generator（MIT） |

HTTP API：`GET /api/state`、`POST /api/config`（JSON）、`POST /api/time`、`POST /api/assets`（`X-Asset-Name: card|about|name|avatar`，JPEG，先写 `.tmp` 再替换）、`POST /api/reset`、`GET /avatar.jpg`。

## 在电脑上预览

```bash
# 编辑器 + 模拟 API（默认资料同样来自 profile），发布的图层写到 .cache/dev_assets
python3 tools/dev_server.py --port 8766

# 用固件同一份页面代码渲染所有页面，检查二维码回读和路由逻辑
cmake -S sim -B sim/build && cmake --build sim/build -j
sim/build/badge_sim --assets .cache/dev_assets --out .cache/sim --theme 0
```

## 真机验证清单

- [ ] 设置页和扫码页二维码显示正常，串口出现 `QR readback OK`；手机能扫出内容
- [ ] 多次在名片页、扫码页、介绍页、待机页之间切换，画面不再错位
- [ ] iPhone 与安卓扫 Wi-Fi 码后自动打开编辑器；安卓认证窗口里能否选图片
- [ ] 展会灯光下，用微信在 30 cm / 60 cm / 1 m 扫扫码页
- [ ] 触摸方向：左滑应进入介绍页（相反时调换 `input.cpp` 中的判断）
- [ ] 振动马达（M5IOE1 PYG9）手感与时长
- [ ] 续航估算与实际放电时间对比，必要时调整 `power.cpp` 里的模型常数
