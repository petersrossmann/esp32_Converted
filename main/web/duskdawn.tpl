<!DOCTYPE html><html><head><title>Dusk/Dawn</title>
<style>
  body { font-family: sans-serif; margin: 2em; background: #222; color: #eee; }
  h1 { color: #0af; }
  input { background: #444; color: #fff; border: 1px solid #666; padding: 4px; width: 2.5em; text-align: center; }
  table { font-size: 0.8em; margin: 1em 0; }
  .btn { background: #0af; color: #fff; border: none; padding: 0.6em 1.2em; border-radius: 4px; cursor: pointer; width: auto; }
  td { text-align: center; }
</style>
</head><body>
<h1>Dusk/Dawn Offsets</h1>
<form method="post" action="duskdawn.cgi">
<table border="1">
<tr><th>#</th><th>Active</th><th>Dawn</th><th>Dusk</th><th>Mon</th><th>Tue</th><th>Wed</th><th>Thu</th><th>Fri</th><th>Sat</th><th>Sun</th><th>ON(min)</th><th>OFF(min)</th></tr>
%sched_rows%
</table>
<input class="btn" type="submit" value="Save">
</form>
<p><button class="btn" onclick="location.href='/'">Home</button></p>
</body></html>