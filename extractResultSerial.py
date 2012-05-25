#encoding: utf-8

import serial as s

class MalformedDataException(Exception):
    pass

class TimeOutException(Exception):
    pass

class BatteryReader (s.Serial):
    def __init__(self, _port, _timeout, _TIME_BETWEEN_ADC, _VOLT_MAX):
        s.Serial.__init__(self, port = _port, timeout=_timeout)
        self.TIME_BETWEEN_ADC = _TIME_BETWEEN_ADC
        self.VOLT_MAX = _VOLT_MAX;
        
    def readuint8(self):
        data = self.readchar()
        return ord(data)

    def readchar(self):
        data = self.read()
        if len(data) == 0:
            raise TimeOutException()
        return data
    
    def readEvent(self):
        listeMesures = []
        nbMeasurements = self.readuint8()
        temps = 0
        for i in range(nbMeasurements):
            temps += self.readuint8()#* self.TIME_BETWEEN_ADC
            mesure = self.readuint8() * (self.VOLT_MAX / 256.0)
            listeMesures.append([temps, mesure])
        bitFin = self.readuint8()    
        if(bitFin != 0xff):
            raise MalformedDataException()
            
        return listeMesures


print "Démarrage"


try:
    objBatt = BatteryReader(2, 45, 1, 2.5)
    listEvents = objBatt.readEvent()
    print "Résolution : +/- %.3f"%(objBatt.VOLT_MAX / 256.0)
    for t in listEvents:
        print "%d min. : %.2f V" %(t[0], t[1])
    
    print " "
    for t in listEvents:
        print "%d; "%(t[0],),
    print ""
    for t in listEvents:
        print "%.2f; "%(t[1],),
except TimeOutException:
    print "Pas de réception pendant trop longtemps !"
except MalformedDataException:
    print "Data malformé"
except KeyboardInterrupt, SystemExit:
    print "On quitte"
except Exception, e:
    print "Autre Exception"
    print e
finally:
    objBatt.close()