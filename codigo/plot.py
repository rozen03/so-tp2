#!/usr/bin/python3
# -*- coding: utf-8 -*-
import numpy as np
import matplotlib.pyplot as plt
from numpy import genfromtxt
my_data = genfromtxt('0.txt', delimiter=',')
import matplotlib.pyplot as plt
import csv
def plott(index):
	x = []
	y = []
	with open(str(index)+'.txt','r') as csvfile:
		plots = csv.reader(csvfile, delimiter=',')
		for row in plots:
			x.append(int(row[0]))
			y.append(int(row[1]))
	plt.plot(x,y, label=str(index))

for i in range(4):
	plott(i)





plt.xlabel('index')
plt.ylabel('node owner')
plt.title('Interesting Graph\nCheck it out')
plt.legend()
plt.show()
# print(my_data[0])
print("hola")
