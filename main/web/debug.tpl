<!DOCTYPE html><html><head><title>Debug</title>
<style>body{font-family:sans-serif;margin:2em;background:#222;color:#eee;}h1{color:#0af;}input{background:#444;color:#fff;border:1px solid #666;padding:4px;}textarea{background:#444;color:#fff;border:1px solid #666;font-family:monospace;}.btn{background:#0af;color:#fff;border:none;padding:0.6em 1.2em;border-radius:4px;cursor:pointer;}</style>
</head><body>
<h1>Debug</h1>
<p>Version: %ver%</p>
<p>Free heap: %heap%</p>
<p>Time: %time%</p>
<p>Temp: %temp% C &nbsp; Hum: %hum% %</p>
<p>Relay: %relay%</p>
<p>Debug Level: %DebugLevel%</p>
<h2>SMTP Mail Test</h2>
<p><button class="btn" type="button" onclick="testMail()">Test-E-Mail senden</button> <span id="mailStatus" style="font-weight:bold;margin-left:10px;"></span></p>
<script>
function testMail() {
  var st = document.getElementById('mailStatus');
  st.textContent = 'Sende Test-Mail...';
  fetch('/testmail')
    .then(function(r) { return r.text(); })
    .then(function(t) { st.textContent = t; })
    .catch(function(e) { st.textContent = 'Fehler: ' + e; });
}
</script>
<p><button class="btn" onclick="location.href='/'">Home</button></p>
</body></html>