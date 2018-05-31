#!/usr/bin/python3
# -*- coding: utf-8 -*-(
import subprocess
procesos=4
command=["mpirun -np "+ str(procesos)+" ./blockchain"]
f=open("exp.csv", mode="a")
def probar():
	try:
		subprocess.check_call(command,stdout=None,shell=True,timeout=30)
		for i in range(procesos):
			with open(str(i)+".txt") as ff:
				line=ff.readline()
				print(line)
				f.write(line)
		print("termine bien")
		return True
	except Exception as inst:
		print(str(inst))
		print("EXODIA")
		return False
for i in range(1):
	while(not probar()):
		pass

print("LISTO")
f.close()
