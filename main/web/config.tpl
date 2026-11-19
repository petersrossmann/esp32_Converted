<!DOCTYPE html>
<html><head><title>Config</title>
<style>body{font-family:sans-serif;margin:2em;background:#222;color:#eee;}h1{color:#0af;}input{background:#444;color:#fff;border:1px solid #666;padding:4px;}.btn{background:#0af;color:#fff;border:none;padding:0.6em 1.2em;border-radius:4px;cursor:pointer;}</style>
</head><body>
<div id="main">
<h1>Config</h1>
<form method="get" action="config.cgi">
<p>Location: <input maxlength="30" name="Depl1" size="20" type="text" value="%deploy%" /></p>
<p>Latitude <input maxlength="10" name="Lat1" size="10" type="text" value="%lat%" /> Longitude <input maxlength="10" name="Lon1" size="10" type="text" value="%lon%" /></p>
<p>NTP Server <input maxlength="30" name="NtpServer" size="15" type="text" value="%NtpServer%" /> Timezone <input maxlength="3" name="Timezone" size="1" type="text" value="%Timezone%" /> Auto DST <input name="DST" type="checkbox" %DST% /></p>
<p>Debug Level <input maxlength="16" name="DebugLevel" size="10" type="text" value="%DebugLevel%" /></p>
<p>Temperature Offset <input maxlength="8" name="TemperatureOffset" size="10" type="text" value="%TemperatureOffset%" /> Degrees/100</p>
<p>Humidity Offset <input maxlength="8" name="HumidityOffset" size="10" type="text" value="%HumidityOffset%" /> Percent/10</p>
<p>Display Timeout <input maxlength="8" name="DisplayTimeout" size="10" type="text" value="%DisplayTimeout%" /> seconds</p>
<p>SmtpServer <input maxlength="30" name="serv" size="15" type="text" value="%SmtpServer%" /> Port <input maxlength="5" name="port" size="5" type="text" value="%SmtpPort%" /> Sender <input maxlength="30" name="sender" size="20" type="text" value="%SmtpSender%" /> Password <input maxlength="30" name="passwd" size="20" type="text" value="%SmtpPassword%" /> Recipient <input maxlength="30" name="recipi" size="20" type="text" value="%SmtpRecipient%" /></p>
<p>OTA Server <input maxlength="30" name="OTA_serv" size="15" type="text" value="%OtaServer%" /> Port <input maxlength="5" name="OTA_port" size="5" type="text" value="%OtaPort%" /></p>
<p>Display Activity from <input maxlength="2" name="DspAct0" size="1" type="text" value="%DspAct0%" />:<input maxlength="2" name="DspAct1" size="1" type="text" value="%DspAct1%" /> to <input maxlength="2" name="DspAct2" size="1" type="text" value="%DspAct2%" />:<input maxlength="2" name="DspAct3" size="1" type="text" value="%DspAct3%" /></p>
<p>Instant Reset <input name="InstantReset" type="checkbox" /></p>
<input class="btn" type="submit" value="Save Settings">
</form>

<h2>WiFi Setup</h2>
<form action="/wifi" method="get">
<p>SSID <input maxlength="32" name="ssid" size="15" type="text" value=""> Password <input maxlength="64" name="pass" size="15" type="password" value=""> <button class="btn" type="submit">Save &amp; Reboot</button></p>
</form>

<h2>Firmware OTA Upload</h2>
<p><input type="file" id="otaFile" accept=".bin"> <button class="btn" type="button" onclick="uploadOTA()">Upload &amp; Reboot</button></p>
<p><button class="btn" type="button" onclick="checkServerOTA()">Nach Server-Update suchen</button></p>
<p id="otaStatus" style="font-weight:bold;"></p>
<script>
function uploadOTA() {
  var inp = document.getElementById('otaFile');
  if (!inp.files || !inp.files[0]) { alert('Bitte zuerst eine .bin-Datei auswählen!'); return; }
  var file = inp.files[0];
  var st = document.getElementById('otaStatus');
  st.textContent = 'Lade hoch (' + file.size + ' Bytes)...';
  fetch('/update', { method: 'POST', body: file })
    .then(function(r) { return r.text(); })
    .then(function(t) { st.textContent = t; })
    .catch(function(e) { st.textContent = 'Fehler: ' + e; });
}
function checkServerOTA() {
  var st = document.getElementById('otaStatus');
  st.textContent = 'Prüfe Server auf neue Firmware...';
  fetch('/ota_check')
    .then(function(r) { return r.text(); })
    .then(function(t) { st.textContent = t; })
    .catch(function(e) { st.textContent = 'Fehler: ' + e; });
}

</script>

<p><button class="btn" onclick="location.href='/'">Home</button></p>
</div></body></html>