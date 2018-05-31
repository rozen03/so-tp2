#!/usr/bin/python3
# -*- coding: utf-8 -*-(
import subprocess

f=open("exp15.csv", mode="w")
def probar(procesos):
	try:
		logs = open("logs.txt", "w")
		command=["mpirun -np "+ str(procesos)+" ./blockchain"]
		subprocess.check_call(command,stdout=logs,shell=True,timeout=3000)
		commanDOS=["cat logs.txt | grep 'Conflicto suave' | wc -l"]
		proc=subprocess.Popen(commanDOS,stdout=subprocess.PIPE,shell=True)
		output = proc.stdout.read().strip().decode("utf-8")
		f.write(str(procesos)+","+output+"\n")
		logs.close()
		print("termine bien")
		return True
	except Exception as inst:
		print(str(inst))
		print("EXODIA")
		return False
repeticiones=10
for i in range(repeticiones):
	for n in [5,10,20,50]:
		print("Repeticion:"+str(i)+", Nodos:"+str(n))
		while(not probar(n)):
			pass
print("LISTO")
f.close()
