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
.pet-top{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:10px;}
.pet-top h2{margin:0;}.pet-level{font-size:12px;font-weight:700;color:#8a5a38;background:#fff3d9;border-radius:99px;padding:5px 9px;}
.pet-stats{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;margin:12px 0;}
.pet-stats label{margin:0 0 5px;font-size:12px;display:flex;justify-content:space-between;gap:3px;}
.pet-stats progress{display:block;width:100%;height:9px;accent-color:var(--pink);}
.pet-actions{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:7px;}
.pet-actions .btn{font-size:12px;padding:10px 4px;}
.pet .btn:disabled{opacity:.5;cursor:default;}
.pet-hint{font-size:12px;line-height:1.7;color:var(--sub);margin:10px 0 0;}
.pet-feedback{font-size:12px;min-height:18px;margin-top:8px;color:#9d3862;}
.screen-tabs{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:7px;margin:10px 0;}
.screen-tabs button{border:1.5px solid var(--border);border-radius:12px;background:#fff;color:var(--sub);padding:11px 4px;font-size:13px;font-weight:700;cursor:pointer;}
.screen-tabs button[aria-pressed=true]{background:var(--soft);border-color:var(--pink);color:#d63c72;}
.screen-tabs button:disabled,.exchange .btn:disabled,.mario .btn:disabled{opacity:.5;cursor:default;}
.exchange-rate{font-size:31px;font-weight:800;letter-spacing:-.8px;color:#b9386c;margin:8px 0 2px;font-variant-numeric:tabular-nums;}
.exchange-unit{font-size:13px;color:var(--sub);font-weight:600;}
.exchange-info{font-size:12px;line-height:1.8;color:var(--sub);margin:10px 0;}
.exchange-status{font-size:12px;font-weight:700;color:#846233;min-height:18px;}
.hardware summary{cursor:pointer;font-weight:700;font-size:14px;color:#d94b7d;}
.hardware h3{font-size:12.5px;margin:18px 0 8px;color:var(--sub);}
.hardware a{color:#b74e7c;}
.hw-list{display:grid;grid-template-columns:minmax(100px,.8fr) minmax(0,1.7fr);margin:0;font-size:12px;line-height:1.65;}
.hw-list dt,.hw-list dd{margin:0;padding:7px 0;border-bottom:1px solid #f3e7ed;overflow-wrap:anywhere;}
.hw-list dt{color:var(--sub);padding-right:10px;}.hw-list dd{font-weight:600;}
.hw-note{font-size:11.5px;color:var(--sub);line-height:1.7;margin:10px 0 0;}
@media(max-width:460px){.screen-tabs{grid-template-columns:repeat(2,minmax(0,1fr));}}
@media(max-width:370px){.hw-list{grid-template-columns:minmax(90px,.75fr) minmax(0,1.4fr);font-size:11.5px;}}
</style>
</head>
<body>
<div class="hdr">
  <h1>치이카와 독립형 시계 <span class="badge">Standalone NTP</span></h1>
  <div class="status" id="statusBox">장비 상태를 불러오는 중...</div>
</div>

<section class="card" id="screenCard" aria-labelledby="screenHeading" hidden>
  <h2 id="screenHeading">📺 화면 선택</h2>
  <div class="screen-tabs" role="group" aria-label="기기 화면 선택">
    <button type="button" data-screen="clock" aria-pressed="false" onclick="setScreenMode('clock')">시계</button>
    <button type="button" data-screen="pet" aria-pressed="false" onclick="setScreenMode('pet')">다마고치</button>
    <button type="button" data-screen="exchange" aria-pressed="false" onclick="setScreenMode('exchange')">엔화 환율</button>
    <button type="button" data-screen="mario" aria-pressed="false" onclick="setScreenMode('mario')">마리오 시계</button>
  </div>
  <p class="pet-hint">상단 더블 터치: 시계 → 다마고치 → 엔화 환율 → 마리오 시계 → 시계</p>
  <div class="pet-feedback" id="screenFeedback" role="status" aria-live="polite"></div>
</section>

<section class="card mario" id="marioCard" aria-labelledby="marioHeading" hidden>
  <h2 id="marioHeading">🍄 마리오 레트로 시계</h2>
  <div class="status" id="marioStatus">시계 상태를 기다리고 있어요.</div>
  <p class="pet-hint">파란 하늘과 주황색 타이틀 보드의 레트로 화면에 현재 시각과 날짜를 표시합니다.</p>
  <div class="row">
    <button class="btn sec" type="button" id="marioMode" onclick="setScreenMode('mario')">마리오 시계 보기</button>
    <button class="btn sec" type="button" id="marioJump" onclick="jumpMario()" disabled>🍄 점프</button>
  </div>
  <p class="pet-hint">마리오 화면에서 한 번 터치: 점프 · 길게 터치: 시계로 돌아가기<br>더블 터치: 다음 화면<br>VS. SUPER MARIO BROS. 타이틀 화면 스타일</p>
  <div class="pet-feedback" id="marioFeedback" role="status" aria-live="polite"></div>
</section>

<section class="card exchange" id="exchangeCard" aria-labelledby="exchangeHeading" hidden>
  <h2 id="exchangeHeading">🇯🇵 일본 엔화 환율</h2>
  <div class="exchange-unit">100엔 (100 JPY)당 원화</div>
  <div class="exchange-rate" id="exchangeRate">—</div>
  <div class="exchange-status" id="exchangeStatus" role="status" aria-live="polite">환율을 기다리고 있어요.</div>
  <div class="exchange-info">
    <div>환율 기준일: <strong id="exchangeDate">—</strong></div>
    <div>마지막 조회: <span id="exchangeFetched">—</span></div>
    <div>출처: <span id="exchangeSource">ECB via Frankfurter</span></div>
  </div>
  <button class="btn sec" type="button" id="exchangeRefresh" onclick="refreshExchange()">↻ 환율 다시 조회</button>
  <div class="pet-feedback" id="exchangeFeedback" role="status" aria-live="polite"></div>
  <p class="pet-hint">공시 기준환율이며 실시간 시세나 은행의 현찰 매매 환율과 다릅니다.<br>엔화 화면에서 한 번 터치: 다시 조회 (30초 간격) · 길게 터치: 시계로 돌아가기</p>
</section>

<section class="card pet" id="petCard" aria-labelledby="petHeading" hidden>
  <div class="pet-top">
    <h2 id="petHeading">🌱 치이카와 다마고치</h2>
    <span class="pet-level" id="petLevel">Lv.1</span>
  </div>
  <div class="status" id="petActivity">치이카와의 소식을 불러오는 중...</div>
  <div class="pet-stats">
    <div><label for="petFullness">포만감 <span id="petFullnessVal">—</span></label><progress id="petFullness" max="100" value="0"></progress></div>
    <div><label for="petHappiness">기분 <span id="petHappinessVal">—</span></label><progress id="petHappiness" max="100" value="0"></progress></div>
    <div><label for="petEnergy">체력 <span id="petEnergyVal">—</span></label><progress id="petEnergy" max="100" value="0"></progress></div>
  </div>
  <button class="btn" type="button" id="petMode" onclick="petAction(petState&&petState.active?'exit':'enter')">다마고치 모드로 가기</button>
  <div class="pet-actions">
    <button class="btn sec" type="button" data-pet-care="feed" onclick="petAction('feed')" disabled>🍙 밥 주기</button>
    <button class="btn sec" type="button" data-pet-care="play" onclick="petAction('play')" disabled>💗 놀아 주기</button>
    <button class="btn sec" type="button" data-pet-care="sleep" onclick="petAction('sleep')" disabled>🌙 쉬게 하기</button>
  </div>
  <div class="pet-hint" id="petNeeds" hidden></div>
  <p class="pet-hint">상단 더블 터치: 시계 → 다마고치 → 엔화 환율 → 마리오 시계 → 시계<br>게임에서 한 번 터치하면 놀아 주기 · 약 0.9초 누르면 밥 주기</p>
  <div class="pet-feedback" id="petFeedback" role="status" aria-live="polite"></div>
</section>

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

<details class="card hardware" id="hardwareCard" style="margin-top:14px" hidden>
  <summary>🔎 하드웨어 사양</summary>
  <h3>기기에서 읽은 값</h3>
  <dl class="hw-list" id="hardwareMeasured"></dl>
  <p class="hw-note">힙은 현재 펌웨어가 할당에 사용할 수 있는 메모리 영역입니다. 칩 전체 SRAM 용량과 같지 않으며, 여유량은 동작 중 바뀝니다. PSRAM 0 B는 현재 펌웨어에서 감지·초기화되지 않았다는 뜻입니다.</p>
  <p class="hw-note" id="hardwareChipReference" hidden>제조사 문서 사양 (실측 아님): Xtensa LX6 · 내부 SRAM 520 KB (제조사 표기) · Bluetooth 4.2 BR/EDR + LE.<br>칩의 규격상 지원이며, 현재 Bluetooth 펌웨어 활성 여부는 위 상태를 따릅니다. <a href="https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf" target="_blank" rel="noopener noreferrer">Espressif ESP32 데이터시트</a></p>
  <h3>보드·펌웨어 설정</h3>
  <dl class="hw-list" id="hardwareBoard"></dl>
  <p class="hw-note">화면 크기·컨트롤러·GPIO·SPI는 이 보드에 적용한 사양과 설정값입니다. 센서로 자동 식별한 값이 아닙니다. <a href="https://geekmagic.cc/products/smalltv-pro/" target="_blank" rel="noopener noreferrer">공식 SmallTV Pro 제품 사양 (1.54인치 IPS, 240 × 240)</a>. Bluetooth 기능 플래그만으로 규격 버전을 판정하지 않습니다.</p>
</details>

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
  <span id="firmwareVersion">Chiikawa Standalone v1.0</span>
  <a href="/update">📦 펌웨어 업데이트 / 순정 복구 (/update)</a>
</div>

<div class="toast" id="toast">저장되었습니다!</div>

<script>
let curChar='chiikawa', brtTimer=null;
let petState=null, petBusy=false;
let currentScreenMode=null, screenBusy=false, exchangeState=null, exchangeBusy=false;
let marioState=null, marioBusy=false;

function renderPet(p){
  const card=document.getElementById('petCard');
  if(!p || typeof p.active!=='boolean'){ card.hidden=true; petState=null; return; }
  petState=p; card.hidden=false;
  const activities={wander:'이리저리 산책하고 있어요 🌱',happy:'함께 놀아서 신났어요 💗',eating:'냠냠! 맛있게 먹고 있어요 🍙',sleeping:'쿨쿨, 체력을 채우고 있어요 🌙'};
  document.getElementById('petActivity').textContent=p.active?(activities[p.activity]||'치이카와와 함께하는 시간'):'다른 화면에서도 치이카와는 잘 지내고 있어요.';
  document.getElementById('petLevel').textContent=`Lv.${p.level} · 돌봄 ${p.careCount}회`;
  [['Fullness',p.fullness],['Happiness',p.happiness],['Energy',p.energy]].forEach(([key,value])=>{
    const n=Math.max(0,Math.min(100,Number(value)||0));
    document.getElementById('pet'+key).value=n;
    document.getElementById('pet'+key+'Val').textContent=n+'%';
  });
  const mode=document.getElementById('petMode');
  mode.textContent=p.active?'시계 화면으로 돌아가기':'다마고치 모드로 가기';
  mode.disabled=petBusy||screenBusy;
  const needsRest=p.energy<8||p.fullness<4;
  card.querySelectorAll('[data-pet-care]').forEach(b=>{
    const action=b.dataset.petCare;
    b.disabled=petBusy||!p.active||(action==='play'&&needsRest)||(action==='sleep'&&p.activity==='sleeping');
  });
  const needs=document.getElementById('petNeeds');
  needs.hidden=!p.active||!needsRest;
  needs.textContent=needsRest?'밥을 먹고 쉬면 다시 놀 수 있어요. 밥 주기와 쉬게 하기로 돌봐 주세요.':'';
}

function renderScreenMode(mode){
  const card=document.getElementById('screenCard');
  if(!['clock','pet','exchange','mario'].includes(mode)){card.hidden=true;return;}
  currentScreenMode=mode; card.hidden=false;
  card.querySelectorAll('[data-screen]').forEach(button=>{
    button.setAttribute('aria-pressed',String(button.dataset.screen===mode));
    button.disabled=screenBusy||petBusy||marioBusy;
  });
  if(marioState) renderMario(marioState);
}

function renderMario(m){
  const card=document.getElementById('marioCard');
  if(!m || typeof m.timeValid!=='boolean'){card.hidden=true;marioState=null;return;}
  marioState=m;card.hidden=false;
  const timeValid=m.timeValid&&Number.isInteger(m.hour)&&Number.isInteger(m.minute);
  document.getElementById('marioStatus').textContent=timeValid?
    `기기 시각 ${String(m.hour).padStart(2,'0')}:${String(m.minute).padStart(2,'0')} · ${m.jumping?'마리오가 점프하고 있어요!':'타이틀 화면과 함께 움직여요.'}`:
    '시간 동기화를 기다리고 있어요. Wi-Fi 연결을 확인해 주세요.';
  document.getElementById('marioMode').disabled=screenBusy||petBusy||marioBusy||currentScreenMode==='mario';
  document.getElementById('marioJump').disabled=screenBusy||petBusy||marioBusy||currentScreenMode!=='mario'||!!m.jumping;
}

async function jumpMario(){
  if(marioBusy||screenBusy||petBusy||currentScreenMode!=='mario'||!marioState||marioState.jumping) return;
  marioBusy=true;renderMario(marioState);renderScreenMode(currentScreenMode);
  const feedback=document.getElementById('marioFeedback');
  feedback.textContent='마리오에게 점프를 전하고 있어요...';
  try{
    const response=await fetch('/api/mario/jump',{method:'POST'});
    if(!response.ok){
      let detail=null;try{detail=await response.json();}catch(error){}
      throw new Error(detail&&typeof detail.error==='string'?detail.error:'마리오 화면에서 다시 눌러 주세요.');
    }
    const result=await response.json();
    if(result.ok!==true) throw new Error('점프 응답을 확인할 수 없어요.');
    feedback.textContent='마리오가 점프해요! 🍄';
    await fetchDeviceExtras();
    setTimeout(()=>fetchDeviceExtras().catch(()=>{}),1100);
  }catch(error){feedback.textContent=error.message||'기기와 연결하지 못했어요.';}
  finally{marioBusy=false;if(marioState)renderMario(marioState);renderScreenMode(currentScreenMode);}
}

function renderExchange(e){
  const card=document.getElementById('exchangeCard');
  if(!e || typeof e.valid!=='boolean'){card.hidden=true;exchangeState=null;return;}
  exchangeState=e; card.hidden=false;
  const hasRate=e.valid&&typeof e.krwPer100Jpy==='number'&&Number.isFinite(e.krwPer100Jpy)&&e.krwPer100Jpy>0;
  document.getElementById('exchangeRate').textContent=hasRate?e.krwPer100Jpy.toLocaleString('ko-KR',{minimumFractionDigits:2,maximumFractionDigits:2})+' 원':'—';
  document.getElementById('exchangeDate').textContent=hasRate&&e.rateDate?e.rateDate:'—';
  document.getElementById('exchangeFetched').textContent=e.fetchedAt||'—';
  document.getElementById('exchangeSource').textContent=e.source||'ECB via Frankfurter';
  const status=document.getElementById('exchangeStatus');
  status.textContent=e.loading?(hasRate?'조회 중 · 마지막 확인 환율을 표시해요.':'환율을 조회하고 있어요...'):e.error?(hasRate?'저장된 환율 · '+e.error:e.error):!hasRate?'환율을 아직 받지 못했어요.':e.stale?'저장된 환율 · 최신 조회가 필요해요.':'공시 기준환율 조회 완료';
  const button=document.getElementById('exchangeRefresh');
  button.disabled=exchangeBusy||e.loading;
  button.textContent=e.loading||exchangeBusy?'환율 조회 중...':'↻ 환율 다시 조회';
}

async function fetchDeviceExtras(){
  const response=await fetch('/api/status');
  if(!response.ok) throw new Error('기기 상태를 확인하지 못했어요.');
  renderDeviceExtras(await response.json());
}

async function setScreenMode(mode){
  if(screenBusy||petBusy||marioBusy||!['clock','pet','exchange','mario'].includes(mode)) return;
  screenBusy=true; renderScreenMode(currentScreenMode); if(petState) renderPet(petState);
  const feedback=document.getElementById('screenFeedback');
  feedback.textContent='화면을 전환하고 있어요...';
  try{
    const response=await fetch('/api/screen',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({mode})});
    if(!response.ok) throw new Error('화면을 전환하지 못했어요. 다시 시도해 주세요.');
    const result=await response.json();
    if(!['clock','pet','exchange','mario'].includes(result.mode)) throw new Error('기기 화면 응답을 확인할 수 없어요.');
    renderScreenMode(result.mode);
    feedback.textContent={clock:'시계 화면을 표시했어요.',pet:'다마고치 화면을 표시했어요.',exchange:'엔화 환율 화면을 표시했어요.',mario:'마리오 시계 화면을 표시했어요.'}[result.mode];
    await fetchDeviceExtras();
  }catch(error){feedback.textContent=error.message||'기기와 연결하지 못했어요.';}
  finally{screenBusy=false;renderScreenMode(currentScreenMode);if(petState)renderPet(petState);}
}

async function refreshExchange(){
  if(exchangeBusy||!exchangeState||exchangeState.loading) return;
  exchangeBusy=true; renderExchange(exchangeState);
  const feedback=document.getElementById('exchangeFeedback');
  feedback.textContent='환율 조회를 요청하고 있어요...';
  try{
    const response=await fetch('/api/exchange/refresh',{method:'POST'});
    if(!response.ok){
      let detail=null;try{detail=await response.json();}catch(error){}
      if(response.status===429) throw new Error('다시 조회하려면 잠시 기다려 주세요. 수동 조회 간격은 30초예요.');
      throw new Error(detail&&typeof detail.error==='string'?detail.error:'Wi-Fi 연결과 시간 동기화를 확인해 주세요.');
    }
    const result=await response.json();
    if(typeof result.valid!=='boolean') throw new Error('환율 응답을 확인할 수 없어요.');
    renderExchange(result);
    feedback.textContent='조회를 시작했어요. 완료되면 환율이 자동으로 바뀝니다.';
  }catch(error){feedback.textContent=error.message||'기기와 연결하지 못했어요.';}
  finally{exchangeBusy=false;if(exchangeState)renderExchange(exchangeState);}
}

function hardwareBytes(value){
  const n=Number(value);
  if(value===null||value===undefined||!Number.isFinite(n)||n<0) return '확인 불가';
  const exact=Math.trunc(n).toLocaleString('ko-KR')+' B';
  if(n>=1048576) return (n/1048576).toFixed(2).replace(/\.00$/,'')+' MiB ('+exact+')';
  if(n>=1024) return (n/1024).toFixed(2).replace(/\.00$/,'')+' KiB ('+exact+')';
  return exact;
}

function hardwareRows(id,rows){
  const list=document.getElementById(id);
  list.replaceChildren();
  rows.forEach(([label,value])=>{
    const term=document.createElement('dt'), detail=document.createElement('dd');
    term.textContent=label;
    detail.textContent=value===null||value===undefined?'확인 불가':String(value);
    list.append(term,detail);
  });
}

function renderHardware(h){
  const card=document.getElementById('hardwareCard');
  if(!h||typeof h!=='object'){card.hidden=true;return;}
  card.hidden=false;
  document.getElementById('hardwareChipReference').hidden=h.chipModel!=='ESP32-D0WD-V3';
  const number=(n,unit='')=>Number.isFinite(Number(n))&&n!==null&&n!==undefined?Number(n).toLocaleString('ko-KR')+unit:'확인 불가';
  const mhz=n=>Number.isFinite(Number(n))&&n!==null&&n!==undefined?number(Number(n)/1000000,' MHz'):'확인 불가';
  const revision=Number.isInteger(h.chipRevision)?`v${Math.floor(h.chipRevision/100)}.${h.chipRevision%100} (원시값 ${h.chipRevision})`:'확인 불가';
  const radios=[];
  if(h.chipBluetoothClassic===true) radios.push('Classic');
  if(h.chipBLE===true) radios.push('BLE');
  const btHardware=radios.length?radios.join(' · '):'지원 플래그 없음';
  const btFirmware=h.bluetoothFirmwareEnabled===true?'활성':h.bluetoothFirmwareEnabled===false?'비활성':'확인 불가';
  hardwareRows('hardwareMeasured',[
    ['칩 모델',h.chipModel],
    ['칩 리비전',revision],
    ['CPU',number(h.chipCores,' 코어')+' · '+number(h.cpuMHz,' MHz')],
    ['크리스털',number(h.xtalMHz,' MHz')],
    ['플래시 용량',hardwareBytes(h.flashBytes)],
    ['플래시 동작',mhz(h.flashSpeedHz)+' · '+(h.flashMode||'확인 불가')],
    ['힙 전체',hardwareBytes(h.heapTotalBytes)],
    ['힙 여유 (현재)',hardwareBytes(h.heapFreeBytes)],
    ['힙 최소 여유',hardwareBytes(h.heapMinFreeBytes)],
    ['최대 연속 할당',hardwareBytes(h.heapMaxAllocBytes)],
    ['PSRAM 감지 용량',hardwareBytes(h.psramBytes)+(h.psramBytes===0?' · 감지·초기화 안 됨':'')],
    ['PSRAM 여유',hardwareBytes(h.psramFreeBytes)],
    ['앱 이미지 크기',hardwareBytes(h.sketchBytes)],
    ['실행 / OTA 파티션',(h.runningPartition||'확인 불가')+' / '+(h.otaPartition||'확인 불가')],
    ['OTA 슬롯 크기',hardwareBytes(h.otaSlotBytes)],
    ['Wi-Fi 하드웨어',h.chipWifiBGN===true?'802.11 b/g/n 지원':h.chipWifiBGN===false?'지원 플래그 없음':'확인 불가'],
    ['Bluetooth 하드웨어',btHardware],
    ['Bluetooth 펌웨어',btFirmware]
  ]);
  const board=h.board||{}, pins=board.pins||{};
  const gpio=n=>Number.isInteger(n)?(n===-1?'GND에 고정 / 미사용':'GPIO'+n):'확인 불가';
  hardwareRows('hardwareBoard',[
    ['기기 모델 (펌웨어 대상)',h.model],
    ['화면',number(board.displayDiagonalInches,'인치')+' · '+number(board.displayWidth)+' × '+number(board.displayHeight)+' px'],
    ['화면 컨트롤러',board.displayController],
    ['화면 SPI 설정',mhz(board.displaySpiHz)],
    ['SPI MOSI / SCLK',gpio(pins.mosi)+' / '+gpio(pins.sclk)],
    ['화면 DC / RST',gpio(pins.dc)+' / '+gpio(pins.rst)],
    ['화면 CS',gpio(pins.cs)],
    ['백라이트',gpio(pins.backlight)+(board.backlightActiveLow===true?' · Active LOW':'')],
    ['상단 정전식 터치',gpio(pins.touch)]
  ]);
}

function renderDeviceExtras(d){
  renderHardware(d.hardware);
  renderMario(d.mario);
  if(!petBusy) renderPet(d.pet);
  if(!screenBusy) renderScreenMode(d.screenMode);
  if(!exchangeBusy) renderExchange(d.exchange);
  if(d.version) document.getElementById('firmwareVersion').textContent=d.version;
}

async function petAction(action){
  if(petBusy || screenBusy || marioBusy || !petState) return;
  petBusy=true; renderPet(petState); renderScreenMode(currentScreenMode);
  const feedback=document.getElementById('petFeedback');
  feedback.textContent='치이카와에게 전하고 있어요...';
  try{
    const r=await fetch('/api/pet',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action})});
    if(!r.ok){
      if(r.status===429) throw new Error('잠깐만요! 1초 뒤 다시 돌봐 주세요.');
      let detail=null;
      try{ detail=await r.json(); }catch(e){}
      throw new Error(detail&&typeof detail.error==='string'?detail.error:'요청을 처리하지 못했어요. 잠시 뒤 다시 시도해 주세요.');
    }
    const p=await r.json();
    if(typeof p.active!=='boolean') throw new Error('기기 응답을 확인할 수 없어요. 다시 시도해 주세요.');
    renderPet(p);
    if(action==='enter'||action==='exit') renderScreenMode(p.active?'pet':'clock');
    const messages={enter:'치이카와의 작은 방에 도착했어요 🌱',exit:'시계 화면으로 돌아왔어요.',feed:'치이카와에게 밥을 주었어요 🍙',play:'치이카와와 함께 놀았어요 💗',sleep:'치이카와가 편안히 쉬어요 🌙'};
    feedback.textContent=messages[action]||'반영했어요.';
  }catch(e){ feedback.textContent=e.message||'기기와 연결하지 못했어요. 연결을 확인해 주세요.'; }
  finally{ petBusy=false; if(petState) renderPet(petState); renderScreenMode(currentScreenMode); }
}
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
    if(!r.ok) throw new Error('기기 상태를 불러오지 못했어요.');
    const d=await r.json();
    renderDeviceExtras(d);
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
    if(!r.ok) throw new Error('기기 상태를 불러오지 못했어요.');
    const d=await r.json();
    renderDeviceExtras(d);
    document.getElementById('statusBox').innerHTML=
      `현재 시각: <b>${d.time}</b> (${d.ntpSynced?'NTP 동기화 완료 ✓':'NTP 동기화 대기 중...'})<br>`+
      `NTP 서버: <b>${d.ntpServer}</b> | IP: <b>${d.ip}</b> | 날씨: <b>${d.city} ${d.temp}°C</b>`;
  }catch(e){}
}, 5000);
</script>
</body>
</html>
)rawliteral";
