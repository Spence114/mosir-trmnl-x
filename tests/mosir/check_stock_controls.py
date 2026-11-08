"""Guard the stock v1.8.18 controls and hardware configuration against accidental edits."""
import pathlib,re,subprocess
root=pathlib.Path(__file__).resolve().parents[2]
base='4ffa33acbe317e559aa50ee505ffa5fe4ed0272d'
protected=['src/menu.cpp','src/qa.cpp','src/cached_images.cpp','src/sleep_session.cpp',
 'lib/trmnl_x/src/touchbar_gesture.cpp','lib/trmnl_x/include/touchbar_gesture.h',
 'src/power/tca9535_power.cpp','boards/trmnl_x_partitions.csv','boards/esp32s3_n16r8.json',
 'sdkconfigs/sdkconfig.TRMNL_X']
for path in protected:
 original=subprocess.check_output(['git','show',f'{base}:{path}'],cwd=root)
 assert (root/path).read_bytes()==original,f'Stock control or hardware configuration changed: {path}'
# The adapters may call these existing functions, but must not rewrite their semantics.
def function(text,name):
 match=re.search(r'\b'+name+r'\([^;]*?\)\s*\{',text)
 assert match,name
 start=match.start();end=match.end();depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
for path,names in {
 'src/touchbar_actions.cpp':['dispatch_touchbar_intent','process_iqs323_data','touchbar_start','touchbar_poll_ship_check'],
 'lib/trmnl_x/src/iqs323_task.cpp':['iqs323_task_prepare_sleep','iqs323_task_set_gesture_config'],
}.items():
 original=subprocess.check_output(['git','show',f'{base}:{path}'],cwd=root,text=True)
 current=(root/path).read_text()
 for name in names: assert function(original,name)==function(current,name),f'Stock function changed: {name}'
# Preserve every original build environment: the custom environment is appended.
original=subprocess.check_output(['git','show',f'{base}:platformio.ini'],cwd=root)
assert (root/'platformio.ini').read_bytes().startswith(original)
print('Stock v1.8.18 gesture dispatcher, menu, sleep/power controls, partitions, SDK settings, and existing environments match.')
