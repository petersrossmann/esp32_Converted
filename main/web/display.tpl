<!DOCTYPE html><html><head><title>Display</title>
<style>body{font-family:sans-serif;margin:2em;background:#222;color:#eee;}h1{color:#0af;}input{background:#444;color:#fff;border:1px solid #666;padding:4px;}.btn{background:#0af;color:#fff;border:none;padding:0.6em 1.2em;border-radius:4px;cursor:pointer;}textarea{background:#444;color:#fff;border:1px solid #666;}</style>
</head><body>
<h1>Display Control</h1>
<form method="get" action="display.cgi">
<p>Command (T[x,y,font,fg,bg]text):<br>
<input type="text" name="cmd" size="60" value="T[100,100,2,65535,0]Hello World"></p>
<p>Display Activity: %DspAct0%:%DspAct1% to %DspAct2%:%DspAct3%</p>
<input class="btn" type="submit" value="Send">
</form>
<p><button class="btn" onclick="location.href='/'">Home</button></p>
</body></html>