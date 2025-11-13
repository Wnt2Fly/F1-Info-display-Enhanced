
# Formula 1 Info tracker  
  
![main1](https://github.com/user-attachments/assets/bd2d88d6-c71a-4726-ab1c-6443115e69ff) 

## Pole Position before race:  

![pole](https://github.com/user-attachments/assets/883670d9-ccd7-417a-98e4-c7f5ccae7eb7)

## Dashboard     
  
![Dash 1](https://github.com/user-attachments/assets/c9934ac0-f57a-41f1-b868-346afdeda1d2)
   
## About this project  
Formula 1 info screen, is based on esp32 and **2.9"  3 color E-Paper module from weact studio** and ergast API, It allows to track Formula 1 events, dates, points, stats and many more on e-paper display and weserver dashboard.  
  
**Features:**  
  
- mDNS option (**f1tracker.local**), or device IP address for dashboard or OTA  
- Data is fetched from **ergast** API and displayed on the screen every 30 minutes,  
- OTA Uploads  
- Full online dashboard including season calendar, all season rounds, pole positions, driver points etc..  
- Automatic winter/summer time change  
- wifi AP mode for fast wifi setup  
    
**Default data on E-paper:**  
  
- Top 5 Teams including constructors points for current season   
- Top 10 drivers including season points   
- Last race location including podium places   
- Next race location and date  
- Pole position for next race  
  
<p align="center">
<a href="https://www.buymeacoffee.com/mazur888" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/default-orange.png" alt="Buy Me A Coffee" height="35" width="auto"></a>
</p>
  
## **Hardware:**  
E-Paper module: [2.9 Inch 3-color weact studio e-paper module](https://www.aliexpress.com/item/1005005183232092.html?spm=a2g0o.productlist.main.2.d29e43e0C0sZmD&algo_pvid=ab08f407-4a25-4efe-bcc8-adf1ec59051f&algo_exp_id=ab08f407-4a25-4efe-bcc8-adf1ec59051f-1&pdp_ext_f=%7B%22order%22%3A%22740%22%2C%22eval%22%3A%221%22%7D&pdp_npi=4%40dis%21GBP%2126.76%217.09%21%21%21251.74%2166.72%21%40211b813b17533132046734689e8b5e%2112000032024103612%21sea%21UK%210%21ABX&curPageLogUid=xo1fotgOXa89&utparam-url=scene%3Asearch%7Cquery_from%3A)  
ESP32: [Esp32 dev board](https://www.aliexpress.com/item/1005005476182210.html?spm=a2g0o.productlist.main.9.22806093i8gCEe&algo_pvid=237645af-105e-4264-bbb2-4e233f1f3b1e&algo_exp_id=237645af-105e-4264-bbb2-4e233f1f3b1e-8&pdp_ext_f=%7B%22order%22%3A%2220%22%2C%22eval%22%3A%221%22%7D&pdp_npi=4%40dis%21GBP%212.89%210.99%21%21%213.79%211.29%21%40211b629217533130616098320efa33%2112000041163128652%21sea%21UK%210%21ABX&curPageLogUid=WzkdBpQJJBM4&utparam-url=scene%3Asearch%7Cquery_from%3A)
  
**ESP32 to Screeen Connections:**  
BUSY = 4  
CS = 5  
RST = 16  
DC = 17  
SCK = 18  
MISO = 19  
MOSI = 23  
  
## **Software:**
**WiFI access point login details:**  
- SSID "F1 display"  
- PASS "formula1"  
  
**Time zones:**  
- Change time zone to match your location. default time zone is BST for United Kingdom  
  
MET-1METDST,M3.5.0/01,M10.5.0/02   //Europe  
CET-1CEST,M3.5.0,M10.5.0/3         // Central Europe  
EST-2METDST,M3.5.0/01,M10.5.0/02   // Europe  
EST5EDT,M3.2.0,M11.1.0           // EST USA  
CST6CDT,M3.2.0,M11.1.0           // CST USA  
MST7MDT,M4.1.0,M10.5.0           // MST USA  
NZST-12NZDT,M9.5.0,M4.1.0/3      // Auckland  
EET-2EEST,M3.5.5/0,M10.5.5/0     // Asia  
ACST-9:30ACDT,M10.1.0,M4.1.0/3   // Australia  
  
**API Updates:**  
- Screen and dashboard updates are pulled from server every 30 minutes
  
**Software Updates:**  
- If you wish to update the software, OTA server is available under devices IP or f1tracker.local
  
## **Third‑Party Libraries**  
- **GxEPD2** (v1.6.4) by ZinggJM — GPL 3.0  
- **U8g2_for_Adafruit_GFX** (v1.8.0) by olikraus — BSD 2‑Clause  
- **WiFiManager** (v2.0.17) by tzapu — MIT  
- **ArduinoJson** (v7.4.2) by Benoit Blanchon — MIT

## Enclosure
- STL Files are included in this project  
  
**Printer:**  
Any printer will be ok, For this project I used Creality K1C and hyper pla  
all settings default in creality print  
printed with no supports  
  
## License
This project is released under the [GNU GPL v3.0](LICENSE).  
© 2025 Mazur888. No warranty; use at your own risk.

  
<p align="center">
<a href="https://www.buymeacoffee.com/mazur888" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/default-orange.png" alt="Buy Me A Coffee" height="35" width="auto"></a>
</p>
