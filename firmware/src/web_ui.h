#pragma once
#include <Arduino.h>
#include <pgmspace.h>

const char WEB_INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>치이카와 독립형 시계 설정</title>
<style>
:root{--pink:#ff6b9d;--soft:#fff0f5;--bg:#fdf6f9;--card:#ffffff;--text:#3d2b35;--sub:#8c6d7b;--border:#ffd4e3;}
*{box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,"Apple SD Gothic Neo","Malgun Gothic",sans-serif;}
body{margin:0;padding:16px;background:var(--bg);color:var(--text);max-width:560px;margin-inline:auto;}
.hdr{background:linear-gradient(135deg,#ffe0ec,#e2f1ff);border-radius:20px;padding:18px;margin-bottom:14px;border:2px solid #fff;box-shadow:0 6px 18px rgba(255,107,157,.12);}
.hdr h1{margin:0 0 6px;font-size:20px;display:flex;align-items:center;gap:8px;}
.status{font-size:12.5px;color:var(--sub);line-height:1.6;}
.badge{display:inline-block;padding:2px 8px;border-radius:99px;background:var(--pink);color:#fff;font-size:11px;font-weight:700;}
.card{background:var(--card);border:1.5px solid var(--border);border-radius:18px;padding:16px;margin-bottom:14px;box-shadow:0 4px 12px rgba(0,0,0,.03);}
.card h2{margin:0 0 12px;font-size:15px;color:#d94b7d;}
label{display:block;font-size:12.5px;font-weight:600;margin:10px 0 5px;color:var(--sub);}
select,input[type=text],input[type=password],input[type=date],input[type=number]{width:100%;padding:10px 12px;border:1.5px solid #ead5de;border-radius:11px;font-size:14px;background:#fff;color:var(--text);}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
.chips{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;}
.chip{padding:10px;border:1.5px solid #ead5de;border-radius:12px;text-align:center;font-size:13.5px;font-weight:700;cursor:pointer;background:#fff;transition:.15s;}
.chip.active{border-color:var(--pink);background:var(--soft);color:#d63c72;}
.btn{width:100%;padding:13px;border:0;border-radius:13px;background:var(--pink);color:#fff;font-size:15px;font-weight:700;cursor:pointer;margin-top:8px;}
.btn.sec{background:#eef4fb;color:#3d5a80;font-size:13px;padding:10px;}
.slider-wrap{display:flex;align-items:center;gap:12px;}
input[type=range]{flex:1;accent-color:var(--pink);}
.toast{position:fixed;bottom:18px;left:50%;transform:translateX(-50%);background:#3d2b35;color:#fff;padding:10px 18px;border-radius:99px;font-size:13px;opacity:0;pointer-events:none;transition:.25s;z-index:99;}
.toast.show{opacity:.95;}
.footer-links{display:flex;justify-content:space-between;font-size:12px;color:var(--sub);margin:16px 4px 28px;}
.footer-links a{color:#d94b7d;text-decoration:none;font-weight:700;}
</style>
</head>
<body>
<div class="hdr">
  <h1>치이카와 독립형 시계 <span class="badge">Standalone NTP</span></h1>
  <div class="status" id="statusBox">장비 상태를 불러오는 중...</div>
</div>

<div class="card">
  <h2>💡 화면 밝기 (즉시 반영)</h2>
  <div class="slider-wrap">
    <input type="range" id="brt" min="3" max="100" value="85" oninput="brtVal.textContent=this.value+'%';liveBrt(this.value)">
    <strong id="brtVal" style="min-width:44px;text-align:right">85%</strong>
  </div>
  <div class="row" style="margin-top:8px">
    <label style="display:flex;align-items:center;gap:6px;margin:0"><input type="checkbox" id="nightDim"> 심야 자동 저밝기 (00~07시)</label>
    <label style="display:flex;align-items:center;gap:6px;margin:0"><input type="checkbox" id="anim" checked> 캐릭터 애니메이션</label>
  </div>
</div>

<div class="card">
  <h2>🎀 캐릭터 & 컬러 테마</h2>
  <label>캐릭터 선택</label>
  <div class="chips" id="charChips">
    <div class="chip" data-v="chiikawa">치이카와</div>
    <div class="chip" data-v="hachiware">하치와레</div>
    <div class="chip" data-v="usagi">우사기</div>
    <div class="chip" data-v="trio">삼총사 함께!</div>
  </div>
  <label>배경 테마</label>
  <select id="theme">
    <option value="pink">치이카와 피치핑크</option>
    <option value="sky">하치와레 소다블루</option>
    <option value="butter">우사기 커스터드옐로우</option>
    <option value="lavender">모모가 라벤더드림</option>
    <option value="night">별빛 파자마 나이트</option>
  </select>
</div>

<div class="card">
  <h2>🕒 NTP 자동 시간 & 날씨</h2>
  <div class="row">
    <div>
      <label>시간 표시</label>
      <select id="use12h">
        <option value="0">24시간제 (14:30)</option>
        <option value="1">12시간제 (02:30 PM)</option>
      </select>
    </div>
    <div>
      <label>초(:SS) 표시</label>
      <select id="showSec">
        <option value="1">초 숫자 + 콜론 깜빡임</option>
        <option value="0">콜론 깜빡임만</option>
      </select>
    </div>
  </div>
  <label>NTP 표준 시간 서버 (기본: time.google.com)</label>
  <input type="text" id="ntpServer" placeholder="time.google.com">
  <label>날씨 지역 (Open-Meteo 실시간 연동)</label>
  <select id="cityPreset" onchange="onCityPreset()">
    <option value="서울|37.5665|126.9780">서울</option>
    <option value="부산|35.1796|129.0756">부산</option>
    <option value="인천|37.4563|126.7052">인천</option>
    <option value="대구|35.8714|128.6014">대구</option>
    <option value="대전|36.3504|127.3845">대전</option>
    <option value="광주|35.1595|126.8526">광주</option>
    <option value="수원|37.2636|127.0286">수원</option>
    <option value="성남|37.4200|127.1267">성남</option>
    <option value="제주|33.4996|126.5312">제주</option>
    <option value="도쿄|35.6762|139.6503">도쿄</option>
    <option value="custom">직접 입력</option>
  </select>
  <div class="row" id="customGeo" style="display:none;margin-top:6px">
    <input type="text" id="cityName" placeholder="도시명 (예: 판교)">
    <input type="text" id="latLon" placeholder="위도,경도 (37.39,127.11)">
  </div>
  <div class="row" style="margin-top:8px">
    <button class="btn sec" type="button" onclick="triggerSync('/api/sync_ntp','NTP 서버와 즉시 동기화했습니다!')">⚡ NTP 즉시 동기화</button>
    <button class="btn sec" type="button" onclick="triggerSync('/api/sync_weather','실시간 날씨를 갱신했습니다!')">🌤️ 날씨 즉시 갱신</button>
  </div>
</div>

<div class="card">
  <h2>💬 하단 응원 문구 & D-Day</h2>
  <label>하단 문구 (모든 한글·기호 완벽 지원)</label>
  <input type="text" id="customText" maxlength="28" placeholder="오늘도 화이팅! ♡">
  <div class="row">
    <div>
      <label>D-Day 이름</label>
      <input type="text" id="ddayTitle" maxlength="10" placeholder="여행 / 생일">
    </div>
    <div>
      <label>D-Day 날짜</label>
      <input type="date" id="ddayDate">
    </div>
  </div>
</div>

<button class="btn" type="button" onclick="saveAll()">💾 설정 저장하기 (맥북 꺼도 영구 유지)</button>

<details class="card" style="margin-top:14px">
  <summary style="cursor:pointer;font-weight:700;font-size:14px;color:var(--sub)">⚙️ 고급 화면 / Wi-Fi 변경</summary>
  <div class="row" style="margin-top:10px">
    <div>
      <label>화면 회전</label>
      <select id="rotation">
        <option value="0">0° (기본 정방향)</option>
        <option value="1">90° 회전</option>
        <option value="2">180° 상하반전</option>
        <option value="3">270° 회전</option>
      </select>
    </div>
    <div>
      <label>IPS 색상 반전</label>
      <select id="invertDisp">
        <option value="1">켜짐 (ST7789 표준)</option>
        <option value="0">꺼짐</option>
      </select>
    </div>
  </div>
  <label>새 Wi-Fi SSID (변경 시에만 입력)</label>
  <input type="text" id="wifiSsid" placeholder="현재 연결된 공유기 유지 시 비워두세요">
  <label>새 Wi-Fi 비밀번호</label>
  <input type="password" id="wifiPass" placeholder="비밀번호">
  <button class="btn sec" type="button" onclick="saveWifi()" style="margin-top:10px">📶 새 Wi-Fi 저장 및 재연결</button>
</details>

<div class="footer-links">
  <span>Chiikawa Standalone v1.0</span>
  <a href="/update">📦 펌웨어 업데이트 / 순정 복구 (/update)</a>
</div>

<div class="toast" id="toast">저장되었습니다!</div>

<script>
let curChar='chiikawa', brtTimer=null;
document.querySelectorAll('#charChips .chip').forEach(el=>{
  el.onclick=()=>{
    curChar=el.dataset.v;
    document.querySelectorAll('#charChips .chip').forEach(c=>c.classList.toggle('active',c.dataset.v===curChar));
  };
});

function showToast(msg){
  const t=document.getElementById('toast');
  t.textContent=msg; t.classList.add('show');
  setTimeout(()=>t.classList.remove('show'),2400);
}

function liveBrt(val){
  clearTimeout(brtTimer);
  brtTimer=setTimeout(()=>fetch('/set?brt='+val),120);
}

function onCityPreset(){
  const v=document.getElementById('cityPreset').value;
  document.getElementById('customGeo').style.display=(v==='custom')?'grid':'none';
}

function calcDdayStr(dateStr){
  if(!dateStr) return '';
  const parts=dateStr.split('-');
  if(parts.length!==3) return '';
  const target=new Date(+parts[0],+parts[1]-1,+parts[2]);
  const now=new Date();
  const today=new Date(now.getFullYear(),now.getMonth(),now.getDate());
  const diff=Math.round((target-today)/86400000);
  if(diff===0) return 'D-DAY';
  return diff>0 ? ('D-'+diff) : ('D+'+Math.abs(diff));
}

// Render arbitrary Korean text to a crisp 200x18 4-bit grayscale alpha bitmap on client <canvas>
// so ALL 11,172 Hangul syllables & emojis look razor-sharp on the ESP8266 without a 4MB font file!
function renderMsgBitmapHex(text){
  const W=196, H=18;
  const cv=document.createElement('canvas');
  cv.width=W*2; cv.height=H*2;
  const ctx=cv.getContext('2d');
  ctx.clearRect(0,0,cv.width,cv.height);
  if(!text) return '';
  ctx.fillStyle='#ffffff';
  ctx.textBaseline='middle';
  let fsz=25;
  while(fsz>16){
    ctx.font=`700 ${fsz}px -apple-system,"Apple SD Gothic Neo","Malgun Gothic",sans-serif`;
    if(ctx.measureText(text).width <= cv.width-8) break;
    fsz-=2;
  }
  const tw=ctx.measureText(text).width;
  const tx=Math.max(4, (cv.width-tw)/2);
  ctx.fillText(text, tx, cv.height/2 + 1);

  const sm=document.createElement('canvas');
  sm.width=W; sm.height=H;
  const sctx=sm.getContext('2d');
  sctx.drawImage(cv,0,0,W,H);
  const img=sctx.getImageData(0,0,W,H).data;
  let hex='';
  for(let i=0;i<W*H;i+=2){
    const a1=Math.min(15, (img[i*4+3]+8)>>4);
    const a2=Math.min(15, (img[(i+1)*4+3]+8)>>4);
    const b=(a1<<4)|a2;
    hex+=b.toString(16).padStart(2,'0');
  }
  return hex;
}

async function loadState(){
  try{
    const r=await fetch('/api/status');
    const d=await r.json();
    document.getElementById('statusBox').innerHTML=
      `현재 시각: <b>${d.time}</b> (${d.ntpSynced?'NTP 동기화 완료 ✓':'NTP 동기화 대기 중...'})<br>`+
      `NTP 서버: <b>${d.ntpServer}</b> | IP: <b>${d.ip}</b> | 날씨: <b>${d.city} ${d.temp}°C</b>`;
    curChar=d.character||'chiikawa';
    document.querySelectorAll('#charChips .chip').forEach(c=>c.classList.toggle('active',c.dataset.v===curChar));
    document.getElementById('theme').value=d.theme||'pink';
    document.getElementById('brt').value=d.brightness;
    document.getElementById('brtVal').textContent=d.brightness+'%';
    document.getElementById('nightDim').checked=!!d.nightDim;
    document.getElementById('anim').checked=!!d.anim;
    document.getElementById('use12h').value=d.use12h?'1':'0';
    document.getElementById('showSec').value=d.showSec?'1':'0';
    document.getElementById('ntpServer').value=d.ntpServer||'time.google.com';
    document.getElementById('customText').value=d.customText||'';
    document.getElementById('ddayTitle').value=d.ddayTitle||'';
    document.getElementById('ddayDate').value=d.ddayDate||'';
    document.getElementById('rotation').value=String(d.rotation||0);
    document.getElementById('invertDisp').value=d.invertDisp?'1':'0';

    const sel=document.getElementById('cityPreset');
    let matched=false;
    for(const opt of sel.options){
      if(opt.value.startsWith(d.city+'|')){ sel.value=opt.value; matched=true; break; }
    }
    if(!matched){
      sel.value='custom';
      document.getElementById('cityName').value=d.city||'서울';
      document.getElementById('latLon').value=`${d.lat},${d.lon}`;
    }
    onCityPreset();
  }catch(e){}
}

async function saveAll(){
  let city='서울', lat=37.5665, lon=126.9780;
  const pv=document.getElementById('cityPreset').value;
  if(pv==='custom'){
    city=document.getElementById('cityName').value.trim()||'서울';
    const ll=(document.getElementById('latLon').value||'37.5665,126.9780').split(',');
    lat=parseFloat(ll[0])||37.5665; lon=parseFloat(ll[1])||126.9780;
  }else{
    const sp=pv.split('|'); city=sp[0]; lat=parseFloat(sp[1]); lon=parseFloat(sp[2]);
  }
  const ctext=document.getElementById('customText').value.trim();
  const dtitle=document.getElementById('ddayTitle').value.trim();
  const ddate=document.getElementById('ddayDate').value.trim();
  const dstr=calcDdayStr(ddate);
  let fullLine='';
  if(dstr && ctext) fullLine=`[${dtitle?dtitle+' ':''}${dstr}] ${ctext}`;
  else if(dstr) fullLine=`${dtitle?dtitle+' ':''}${dstr}`;
  else fullLine=ctext;

  const payload={
    character:curChar,
    theme:document.getElementById('theme').value,
    brightness:parseInt(document.getElementById('brt').value,10),
    nightDim:document.getElementById('nightDim').checked?1:0,
    anim:document.getElementById('anim').checked?1:0,
    use12h:parseInt(document.getElementById('use12h').value,10),
    showSec:parseInt(document.getElementById('showSec').value,10),
    ntpServer:document.getElementById('ntpServer').value.trim()||'time.google.com',
    city, lat, lon,
    customText:ctext,
    ddayTitle:dtitle,
    ddayDate:ddate,
    rotation:parseInt(document.getElementById('rotation').value,10),
    invertDisp:parseInt(document.getElementById('invertDisp').value,10)
  };
  await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)});
  const bmpHex=renderMsgBitmapHex(fullLine);
  await fetch('/api/msg_bmp',{method:'POST',headers:{'Content-Type':'text/plain'},body:bmpHex});
  showToast('설정이 시계에 영구 저장되었습니다! ✨');
  setTimeout(loadState,600);
}

async function triggerSync(url,msg){
  await fetch(url,{method:'POST'});
  showToast(msg);
  setTimeout(loadState,800);
}

async function saveWifi(){
  const ssid=document.getElementById('wifiSsid').value.trim();
  const pass=document.getElementById('wifiPass').value;
  if(!ssid){ showToast('새 Wi-Fi 이름을 입력해주세요.'); return; }
  await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,pass})});
  showToast('새 Wi-Fi로 재연결합니다 (약 10초 소요)...');
}

loadState();
setInterval(async()=>{
  try{
    const r=await fetch('/api/status');
    const d=await r.json();
    document.getElementById('statusBox').innerHTML=
      `현재 시각: <b>${d.time}</b> (${d.ntpSynced?'NTP 동기화 완료 ✓':'NTP 동기화 대기 중...'})<br>`+
      `NTP 서버: <b>${d.ntpServer}</b> | IP: <b>${d.ip}</b> | 날씨: <b>${d.city} ${d.temp}°C</b>`;
  }catch(e){}
}, 5000);
</script>
</body>
</html>
)rawliteral";
