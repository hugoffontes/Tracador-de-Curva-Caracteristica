import sys
from PyQt5 import QtCore, QtGui, uic, QtWidgets
from PyQt5.QtGui import QIcon, QPixmap
import matplotlib.pyplot as mplot
from matplotlib.figure import Figure
import easygui
from tkinter import filedialog
from tkinter import *
import numpy
import serial
qtCreatorFile = "tracer_alpha.ui"
Ui_MainWindow, QtBaseClass = uic.loadUiType(qtCreatorFile)


class MyApp(QtWidgets.QMainWindow, Ui_MainWindow):
    def __init__(self):
        self.linhas = 0
        self.pino = 2
        self.curva = 1
        self.vb = 0
        self.vce = 0
        self.ic = 0
        self.tensao_base = []
        self.tensao_coletor_emissor = []
        self.corrente_coletor = []
        self.texto_composto = ""

        QtWidgets.QMainWindow.__init__(self)
        Ui_MainWindow.__init__(self)
        self.setupUi(self)
        self.pinos_2.setChecked(True)
        self.vbmin.setValue(1.0)
        self.vbmax.setValue(2.1)
        self.vbstep.setValue(0.1)
        self.caminho.setText("/home/hugoff/Documents/ufs/TCC/TCC2/projeto/codigo/gui/diodo.txt")
        #ação dos botões
        self.but_ajuda.clicked.connect(lambda:self.whichbtn(self.but_ajuda))
        self.but_buscar.clicked.connect(lambda:self.whichbtn(self.but_buscar))
        self.but_graficos.clicked.connect(lambda:self.whichbtn(self.but_graficos))
        self.but_iniciar.clicked.connect(lambda:self.whichbtn(self.but_iniciar))
        self.but_sobre.clicked.connect(lambda:self.whichbtn(self.but_sobre))
        self.pinos_2.toggled.connect(lambda:self.whichpino(self.pinos_2))
        self.pinos_3.toggled.connect(lambda:self.whichpino(self.pinos_3))

    def busca_pasta(self):
        root = Tk()
        root.withdraw()
        self.folder_selected = filedialog.asksaveasfilename()
        self.caminho.setText(self.folder_selected)
        with open(self.caminho.text(), 'w') as f:
            f.write("")
            f.close()


    def plot_iv(self):
        mplot.title('Curva I-V')
        mplot.xlabel("Tensão de Coletor (V)");
        mplot.ylabel("Correntes de Coletor (mA)");
        with open(self.caminho.text(),'r') as f:
            print(self.caminho.text())
            self.linhas = [line.rstrip() for line in f]
        print(len(self.linhas))
        for x in range(1,((len(self.linhas))-260),262):
            self.curva = self.linhas[x+1]
            self.tensao_base = self.linhas[x+3]
            for y in range(x,x+260-5,1):
                hold_tensao_coletor_emissor, hold_corrente_coletor = (self.linhas[y+5]).split('\t')
                self.tensao_coletor_emissor.append(float(hold_tensao_coletor_emissor))
                self.corrente_coletor.append(float(hold_corrente_coletor))
            vc = self.tensao_coletor_emissor
            ic = self.corrente_coletor
            mplot.plot(vc,ic)
            self.tensao_coletor_emissor = []
            self.corrente_coletor = []
        mplot.ylim(-30,30)
        f.close()
        mplot.show()

    def serial_com(self):
        ser = serial.Serial('/dev/ttyUSB0',timeout=600)
        print(ser.name)
        # "terminais*vbe_min*vbe_max*vbe_step"
        ser.write((str(self.pino)+str('*')+str((self.vbmin.value())*10)+str('*')+str((self.vbmax.value())*10)+str('*')+str((self.vbstep.value())*10)+str('*\n')).encode('utf-8'))
        pic_output_string = []
        s = ""
        while 1:
            s = ser.read().decode('utf-8')
            if(s == '*'):
                s = '\n'
            if s == '!':
                print("\nbreak\n")
                break
            pic_output_string.append(s)
        with open(self.caminho.text(), 'a') as f:
            f.write(''.join(pic_output_string)) #263 linhas por curva
            f.close()

    def whichbtn(self,b):
        if(b.text() == 'Ajuda'):
            self.terminal.setText("Exibindo Ajuda.")
            easygui.msgbox("Este programa é uma interface dedicada para operação do circuito de extração de curva característica que a acompanha. O processo de uso possui o segiuntes passos:\n\n[PASSO A PASSO].\n\nMais informações estão disponíveis em [REFERÊNCIA].", title="Ajuda")
        if(b.text() == 'Buscar'):
            self.terminal.setText("Definindo local e nome do arquivo a ser salvo...")
            self.busca_pasta()
            self.terminal.setText("Local e nome do arquivo a ser salvo definidos.")
        if(b.text() == 'Exibir gráficos'):
            self.terminal.setText("Exibindo gráficos.")
            self.plot_iv()
        if(b.text() == 'Iniciar'):
            self.terminal.setText("Ensaiando dispositivo...")
            self.serial_com()
            self.terminal.setText("Ensaio concluído.")
        if(b.text() == 'Sobre'):
            self.terminal.setText("Exibindo Sobre.")
            easygui.msgbox("Este programa foi desenvolvido por Hugo Fontes sob orientação do Prof. Dr. Elyson Carvalho, Universidade Federal de Sergipe, 2022.\n\nEste aplicativo Python integra uma interface gráfica e de comunicação serial para controle por computador via porta USB do circuito para extração de curva característica de dispositivos eletrônicos passivos e transistores.\n\nMais informações estão disponíveis em [REFERÊNCIA].", title="Sobre")

    def whichpino(self,p):
        if(p.text() == "2 terminais"):
            if p.isChecked() == True:
                self.pino = 2
        if(p.text() == "3 terminais"):
            if p.isChecked() == True:
                self.pino = 3
        self.terminal.setText("Dispositivo de "+str(self.pino)+" pinos selecionado.")

if __name__ == "__main__":
    app = QtWidgets.QApplication(sys.argv)
    window = MyApp()
    window.show()
    sys.exit(app.exec_())
