import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1360; height: 860; minimumWidth: 1080; minimumHeight: 700
    visible: true; title: "PokitSuite"; color: "#111820"
    property color panel: "#17212b"
    property color line: "#34424d"
    property color ch1: pokit.deviceColor || "#ffd629"
    property color ch2: pokit.deviceColor2 || "#55dff5"
    property bool showDevices: false
    readonly property var meterModes: [
        {label:"Spannung DC", key:"DC Voltage"}, {label:"Spannung AC", key:"AC Voltage"},
        {label:"Strom DC (mA / A)", key:"DC Current"}, {label:"Strom AC (mA / A)", key:"AC Current"},
        {label:"Widerstand", key:"Resistance"}, {label:"Diode", key:"Diode"},
        {label:"Kapazität", key:"Capacitance"}, {label:"Durchgang", key:"Continuity"}
    ]

    component MeterModeBox: ComboBox {
        required property int channel
        model: root.meterModes
        textRole: "label"
        valueRole: "key"
        onActivated: pokit.setMeterMode(channel, currentValue)
    }

    function meterText(value, unit, valid) {
        if (!valid) return "—"
        if (unit === "Durchgang") return value ? "Geschlossen" : "Offen"
        return Number(unit === "F" ? value * 1e6 : value).toLocaleString(Qt.locale("de_DE"), 'f', 4)
    }

    component MeterReadout: Item {
        required property real reading
        required property string unit
        required property bool valid
        required property bool overrange
        Layout.fillWidth: true
        Layout.preferredHeight: 86
        implicitWidth: 320
        Label {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 60
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: parent.overrange ? "OL" : root.meterText(parent.reading, parent.unit, parent.valid)
            color: "#edf6fb"
            font.family: "Consolas"
            font.pixelSize: 48
            elide: Text.ElideRight
        }
        Label {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 24
            horizontalAlignment: Text.AlignHCenter
            text: parent.unit === "F" ? "µF" : parent.unit
            color: "#93a9b8"
            font.pixelSize: 18
        }
    }
    readonly property var deviceColorNames: ["Rot", "Lichtgrau", "Gelb", "Grün"]
    readonly property var deviceColors: ["#ff6666", "#d3d3d3", "#ffd629", "#55df89"]
    property color reference: "#b78cff"
    property real voltsDiv: 1.0
    property int timeDivUs: 10000
    property real verticalPosition: 0.0
    property real triggerPosition: 0.5
    property var timeBases: [10,20,50,100,200,500,1000,2000,5000,10000,20000,50000,100000,200000,500000,1000000]

    function engineeringTime(us) { if(us<1000)return us+" µs/div";if(us<1000000)return(us/1000)+" ms/div";return(us/1000000)+" s/div" }
    function engineeringVoltage(v) { return v<1?Math.round(v*1000)+" mV/div":v+" V/div" }
    function setTimebaseIndex(index) { let i=Math.max(0,Math.min(timeBases.length-1,index));timeBox.currentIndex=i;root.timeDivUs=timeBases[i];pokit.setScopeConfig(coupling.currentText,root.voltsDiv,root.timeDivUs) }
    function metricTime(seconds) { if(seconds<=0)return "—";if(seconds<.001)return(seconds*1000000).toFixed(1)+" µs";if(seconds<1)return(seconds*1000).toFixed(2)+" ms";return seconds.toFixed(2)+" s" }

    header: ToolBar { background: Rectangle { color: "#111a22"; border.color: root.line }
        RowLayout { anchors.fill: parent; anchors.margins: 10
            Rectangle { Layout.preferredWidth:42;Layout.preferredHeight:42;radius:8;color:"#50cdea"; Label { anchors.centerIn:parent;text:"P";color:"#10202a";font.pixelSize:23;font.bold:true } }
            ColumnLayout { spacing:0;Layout.preferredWidth:420;Layout.minimumWidth:420;Layout.maximumWidth:420
                Label { text:"PokitSuite";color:"white";font.pixelSize:19;font.bold:true }
                Label { Layout.fillWidth:true;text:pokit.status;color:"#9eb0bd";font.pixelSize:12;elide:Text.ElideRight }
            }
            Item { Layout.fillWidth:true }
            Label { Layout.preferredWidth:260;horizontalAlignment:Text.AlignRight;elide:Text.ElideRight;text:pokit.connectedDeviceCount>0?"●  "+pokit.connectedDeviceCount+" Pokit"+(pokit.connectedDeviceCount===1?"":"s")+" verbunden":"○  Nicht verbunden";color:pokit.connectedDeviceCount>0?"#4ee6a5":"#91a0aa" }
            Button { Layout.preferredWidth:125;text:pokit.connectedDeviceCount>=2?"2 verbunden":(pokit.connectedDeviceCount===1?"2. suchen":"Verbinden");onClicked:pokit.connectDevice() }
        }
    }

    ColumnLayout { anchors.fill:parent;anchors.margins:12;spacing:8
        RowLayout { Layout.fillWidth:true;spacing:6
            Button { text:"Geräte";highlighted:root.showDevices;onClicked:root.showDevices=true }
            Button { text:"Multimeter";highlighted:!root.showDevices&&pokit.activeFeature==="meter";onClicked:{root.showDevices=false;pokit.showMultimeter()} }
            Button { text:"Oszilloskop";highlighted:!root.showDevices&&pokit.activeFeature==="scope";onClicked:{root.showDevices=false;pokit.showScope()} }
            Item { Layout.fillWidth:true }
            Label { visible:pokit.activeFeature==="scope";text:"Vpp  "+pokit.scopeVpp.toFixed(3)+" V";color:root.ch1;font.bold:true }
            Label { visible:pokit.activeFeature==="scope";text:"f  "+(pokit.scopeFrequency>0?(pokit.scopeFrequency>=1000?(pokit.scopeFrequency/1000).toFixed(2)+" kHz":pokit.scopeFrequency.toFixed(1)+" Hz"):"—");color:"#dce5e9" }
            Label { visible:pokit.activeFeature==="scope";text:"T  "+root.metricTime(pokit.scopePeriod);color:"#dce5e9" }
            Label { visible:pokit.activeFeature==="scope";text:"Zyklen  "+pokit.scopeCycles.toFixed(0);color:"#dce5e9" }
            Label { visible:pokit.activeFeature==="scope";text:"RMS  "+pokit.scopeRms.toFixed(3)+" V";color:"#dce5e9" }
            Label { visible:pokit.activeFeature==="scope";text:"Min/Max  "+pokit.scopeMinimum.toFixed(2)+" / "+pokit.scopeMaximum.toFixed(2)+" V";color:"#dce5e9" }
            Label { visible:pokit.activeFeature==="scope"&&pokit.connected2;text:"CH2  "+pokit.scopeVpp2.toFixed(3)+" Vpp · "+(pokit.scopeFrequency2>0?pokit.scopeFrequency2.toFixed(1)+" Hz":"—");color:root.ch2;font.bold:true }
        }
        StackLayout { currentIndex:root.showDevices?2:(pokit.activeFeature==="meter"?0:1);Layout.fillWidth:true;Layout.fillHeight:true
            Rectangle { color:root.panel;radius:12;border.color:root.line
                ColumnLayout { anchors.fill:parent;anchors.margins:24
                    RowLayout { Layout.fillWidth:true
                        Label { text:"LIVE-MULTIMETER";color:"#8ba0af";Layout.fillWidth:true }
                        ComboBox { model:["0,5 s","1 s","2 s"];onActivated:pokit.setMeterInterval([500,1000,2000][currentIndex]) }
                    }
                    RowLayout { Layout.fillWidth:true;spacing:28
                        ColumnLayout { Layout.fillWidth:true
                            Label { text:"CH1 · "+pokit.deviceName;color:root.ch1;font.bold:true;Layout.alignment:Qt.AlignHCenter }
                            Label { text:pokit.meterSwitch;color:"#93a9b8";Layout.alignment:Qt.AlignHCenter }
                            MeterModeBox { channel:1; Layout.alignment:Qt.AlignHCenter }
                            MeterReadout {
                                reading: pokit.meterValue; unit: pokit.meterUnit
                                valid: pokit.connected && pokit.meterValid
                                overrange: pokit.connected && pokit.meterOverrange
                            }
                        }
                        Rectangle { Layout.preferredWidth:1;Layout.fillHeight:true;color:root.line }
                        ColumnLayout { Layout.fillWidth:true
                            Label { text:"CH2 · "+pokit.deviceName2;color:root.ch2;font.bold:true;Layout.alignment:Qt.AlignHCenter }
                            Label { text:pokit.meterSwitch2;color:"#93a9b8";Layout.alignment:Qt.AlignHCenter }
                            MeterModeBox { channel:2; Layout.alignment:Qt.AlignHCenter }
                            MeterReadout {
                                reading: pokit.meterValue2; unit: pokit.meterUnit2
                                valid: pokit.connected2 && pokit.meterValid2
                                overrange: pokit.connected2 && pokit.meterOverrange2
                            }
                        }
                    }
                    Canvas { id:meterCanvas;Layout.fillWidth:true;Layout.fillHeight:true
                        visible: !pokit.connected || !pokit.connected2 || pokit.meterUnit === pokit.meterUnit2
                        Connections { target:pokit;function onMeterHistoryChanged(){meterCanvas.requestPaint()} }
                        Connections { target:root;function onCh1Changed(){meterCanvas.requestPaint()} function onCh2Changed(){meterCanvas.requestPaint()} }
                        onPaint:{let c=getContext("2d"),a=pokit.meterHistory,b=pokit.meterHistory2;c.reset();c.strokeStyle="#2a3e4b";for(let i=0;i<5;i++){let y=i*height/4;c.beginPath();c.moveTo(0,y);c.lineTo(width,y);c.stroke()}let all=a.concat(b);if(all.length<2)return;let lo=Math.min(...all),hi=Math.max(...all);if(lo===hi){lo--;hi++}function trace(v,color){if(v.length<2)return;c.strokeStyle=color;c.lineWidth=2;c.beginPath();for(let j=0;j<v.length;j++){let x=j*width/(v.length-1),y=(hi-v[j])*height/(hi-lo);j?c.lineTo(x,y):c.moveTo(x,y)}c.stroke()}trace(a,root.ch1);trace(b,root.ch2)}
                    }
                    Label {
                        visible: !meterCanvas.visible && pokit.connected && pokit.connected2
                                 && pokit.meterUnit !== pokit.meterUnit2
                        text: "Der gemeinsame Verlauf benötigt dieselbe Einheit auf beiden Kanälen."
                        color: "#93a9b8"
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            Rectangle { color:"#b8b5a9";radius:8;border.color:"#67675f";border.width:2
                RowLayout { anchors.fill:parent;anchors.margins:10;spacing:10
                    Rectangle { Layout.fillWidth:true;Layout.fillHeight:true;color:"#090d0b";border.color:"#525d54";border.width:5;radius:5
                        ColumnLayout { anchors.fill:parent;anchors.margins:7;spacing:3
                            RowLayout { Layout.fillWidth:true
                                Item { Layout.fillWidth:true }
                                Label { text:pokit.scopeRunning?"● ACQUIRE":"■ STOP";color:pokit.scopeRunning?"#7eff8c":"#ffb56b";font.bold:true }
                                Label { text:pokit.scopeSampleRate>0?Math.round(pokit.scopeSampleRate/1000)+" kS/s":"— kS/s";color:"#b9c6bc" }
                            }
                            Canvas { id:scopeCanvas;Layout.fillWidth:true;Layout.fillHeight:true
                                Connections { target:pokit
                                    function onScopeSamplesChanged(){scopeCanvas.requestPaint()}
                                    function onReferenceChanged(){scopeCanvas.requestPaint()}
                                }
                                Connections { target:root;function onCh1Changed(){scopeCanvas.requestPaint()} function onCh2Changed(){scopeCanvas.requestPaint()} }
                                onWidthChanged:requestPaint();onHeightChanged:requestPaint()
                                onPaint:{
                                    let c=getContext("2d"),a=pokit.scopeSamples,b=pokit.scopeSamples2;c.reset();c.fillStyle="#07100b";c.fillRect(0,0,width,height);c.strokeStyle="#294a34";c.lineWidth=1
                                    for(let i=0;i<=10;i++){let x=i*width/10;c.beginPath();c.moveTo(x,0);c.lineTo(x,height);c.stroke();for(let m=1;m<5&&i<10;m++){let xx=x+m*width/50;c.fillStyle="#25402f";c.fillRect(xx,height/2-2,1,5)}}
                                    for(let k=0;k<=8;k++){let y=k*height/8;c.beginPath();c.moveTo(0,y);c.lineTo(width,y);c.stroke();for(let n=1;n<5&&k<8;n++){let yy=y+n*height/40;c.fillStyle="#25402f";c.fillRect(width/2-2,yy,5,1)}}
                                    c.strokeStyle="#67806d";c.beginPath();c.moveTo(width/2,0);c.lineTo(width/2,height);c.stroke();c.beginPath();c.moveTo(0,height/2);c.lineTo(width,height/2);c.stroke()
                                    let tx=root.triggerPosition*width;c.fillStyle="#ff8c42";c.beginPath();c.moveTo(tx-7,0);c.lineTo(tx+7,0);c.lineTo(tx,10);c.closePath();c.fill()
                                    let zero=height/2-root.verticalPosition/root.voltsDiv*height/8;c.fillStyle=root.ch1;c.beginPath();c.moveTo(0,zero);c.lineTo(10,zero-6);c.lineTo(10,zero+6);c.closePath();c.fill()
                                    let saved=pokit.referenceSamples
                                    function trace(v,color,alpha){if(v.length<2)return;c.globalAlpha=alpha;c.strokeStyle=color;c.lineWidth=2;c.beginPath();for(let q=0;q<v.length;q++){let x=q*width/(v.length-1),y=height/2-((v[q]-root.verticalPosition)/root.voltsDiv)*height/8;q?c.lineTo(x,y):c.moveTo(x,y)}c.stroke();c.globalAlpha=1}
                                    trace(saved,root.reference,.72);trace(b,root.ch2,1);trace(a,root.ch1,1)
                                }
                                MouseArea { anchors.fill:parent;acceptedButtons:Qt.NoButton
                                    onWheel:function(wheel){root.setTimebaseIndex(timeBox.currentIndex+(wheel.angleDelta.y>0?-1:1));wheel.accepted=true}
                                }
                            }
                            RowLayout { Layout.fillWidth:true
                                Label { text:"CH1";color:"#171300";font.bold:true;padding:4;background:Rectangle{color:root.ch1;radius:2} }
                                Label { text:root.engineeringVoltage(root.voltsDiv);color:root.ch1;font.bold:true }
                                Label { visible:pokit.connected2;text:"CH2";color:"#07100b";font.bold:true;padding:4;background:Rectangle{color:root.ch2;radius:2} }
                                Item { Layout.fillWidth:true }
                                Label { text:"M  "+root.engineeringTime(root.timeDivUs);color:"#d8e0d9";font.bold:true }
                                Item { Layout.fillWidth:true }
                                Label { text:"Trig "+triggerMode.currentText+"  "+(triggerLevel.value/100).toFixed(2)+" V";color:"#ff9b58";font.bold:true }
                            }
                        }
                    }

                    Rectangle { Layout.preferredWidth:330;Layout.fillHeight:true;color:"#d0cec4";radius:5;border.color:"#77766f"
                        ScrollView { anchors.fill:parent;anchors.margins:9;clip:true
                            ColumnLayout { width:294;spacing:8
                                Label { text:"POKIT DSO · 4 CHANNEL READY";color:"#26333a";font.pixelSize:15;font.bold:true }
                                RowLayout { Repeater { model:4;Button { required property int index;text:"CH"+(index+1);enabled:index===0||(index===1&&pokit.connected2);highlighted:enabled;Layout.fillWidth:true } } }
                                GroupBox { title:pokit.connected2?"VERTICAL · CH1 + CH2":"VERTICAL · CH1";Layout.fillWidth:true
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { Label{text:"Kopplung";Layout.fillWidth:true} ComboBox{id:coupling;model:["DC","AC"];onActivated:pokit.setScopeConfig(currentText,root.voltsDiv,root.timeDivUs)} }
                                        RowLayout { Label{text:"Volt / Div";Layout.fillWidth:true} ComboBox{id:voltsBox;model:["10 mV","20 mV","50 mV","100 mV","200 mV","500 mV","1 V","2 V","5 V","10 V","20 V","50 V"];currentIndex:6;onActivated:{let v=[.01,.02,.05,.1,.2,.5,1,2,5,10,20,50][currentIndex];root.voltsDiv=v;verticalSlider.from=-4*v;verticalSlider.to=4*v;triggerLevel.from=-400*v;triggerLevel.to=400*v;scopeCanvas.requestPaint();pokit.setScopeConfig(coupling.currentText,v,root.timeDivUs)}} }
                                        Label { text:"Position  "+root.verticalPosition.toFixed(2)+" V" }
                                        Slider { id:verticalSlider;Layout.fillWidth:true;from:-4;to:4;value:0;onMoved:{root.verticalPosition=value;scopeCanvas.requestPaint()} }
                                    }
                                }
                                GroupBox { title:"HORIZONTAL";Layout.fillWidth:true
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { Label{text:"Zeit / Div";Layout.fillWidth:true} ComboBox{id:timeBox;model:["10 µs","20 µs","50 µs","100 µs","200 µs","500 µs","1 ms","2 ms","5 ms","10 ms","20 ms","50 ms","100 ms","200 ms","500 ms","1 s"];currentIndex:9;onActivated:root.setTimebaseIndex(currentIndex)} }
                                        Label { text:"Triggerposition" }
                                        Slider { Layout.fillWidth:true;from:.1;to:.9;value:.5;onMoved:{root.triggerPosition=value;scopeCanvas.requestPaint()} }
                                    }
                                }
                                GroupBox { title:"TRIGGER";Layout.fillWidth:true
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { Label{text:"Modus";Layout.fillWidth:true} ComboBox{id:triggerMode;model:["Auto","Steigend","Fallend"];onActivated:pokit.setScopeTrigger(["free","rising","falling"][currentIndex],triggerLevel.value/100)} }
                                        RowLayout { Label{text:"Quelle";Layout.fillWidth:true} ComboBox{model:pokit.connected2?["je Kanal (CH1 + CH2)"]:["CH1"];enabled:false} }
                                        Label { text:"Pegel  "+(triggerLevel.value/100).toFixed(2)+" V" }
                                        Slider { id:triggerLevel;Layout.fillWidth:true;from:-400;to:400;value:0;stepSize:1;onMoved:pokit.setScopeTrigger(["free","rising","falling"][triggerMode.currentIndex],value/100) }
                                    }
                                }
                                GroupBox { title:"CAPTURE & VERGLEICH";Layout.fillWidth:true
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { Layout.fillWidth:true
                                            Button { text:"PDF + WERTE SPEICHERN";Layout.fillWidth:true;onClicked:pokit.saveScopeCapture() }
                                            Button { text:"ORDNER";onClicked:pokit.openCaptureFolder() }
                                        }
                                        ComboBox { id:captureBox;Layout.fillWidth:true;model:pokit.savedCaptures;textRole:"name";displayText:currentIndex>=0?currentText:"Gespeicherte Aufnahme wählen";onActivated:{let item=pokit.savedCaptures[currentIndex];if(item)pokit.loadScopeCapture(item.path)} }
                                        RowLayout { Layout.fillWidth:true
                                            Label { Layout.fillWidth:true;elide:Text.ElideRight;text:pokit.referenceName.length?"Vergleich: "+pokit.referenceName:"Keine Vergleichskurve";color:pokit.referenceName.length?"#15839a":"#5e676b" }
                                            Button { text:"AUSBLENDEN";enabled:pokit.referenceName.length>0;onClicked:pokit.clearScopeReference() }
                                        }
                                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;font.pixelSize:10;color:"#5e676b";text:"PDF-Bericht, JSON-Aufnahme und CSV-Wertetabelle werden gemeinsam gespeichert." }
                                    }
                                }
                                RowLayout { Layout.fillWidth:true
                                    Button { text:pokit.scopeRunning?"STOP":"RUN";Layout.fillWidth:true;onClicked:pokit.setScopeRunning(!pokit.scopeRunning) }
                                    Button { text:"SINGLE";Layout.fillWidth:true;onClicked:pokit.scopeSingle() }
                                }
                            }
                        }
                    }
                }
            }
            Rectangle {
                color: root.panel; radius: 12; border.color: root.line
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 24; spacing: 14
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Pokit-Geräte"; color: "white"; font.pixelSize: 22; Layout.fillWidth: true }
                        Button { text: "Geräte suchen"; onClicked: pokit.connectDevice() }
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#9eb0bd"
                        text: "Wähle eine Farbe und speichere sie für dieses Gerät. Die Farbe erscheint auch im Multimeter und Oszilloskop."
                    }
                    Label {
                        visible: pokit.devices.length === 0; color: "#9eb0bd"
                        text: "Noch keine Pokits gefunden oder gespeichert."
                    }
                    ListView {
                        id: deviceList
                        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 8; clip: true
                        model: pokit.devices
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            id: deviceCard
                            required property var modelData
                            width: deviceList.width; height: deviceRow.implicitHeight + 24
                            color: "#111a22"; radius: 8; border.color: root.line
                            RowLayout {
                                id: deviceRow
                                anchors.fill: parent; anchors.margins: 12; spacing: 14
                                Rectangle {
                                    Layout.preferredWidth: 14; Layout.preferredHeight: 14; radius: 7
                                    color: root.deviceColors[colorBox.currentIndex] || "#70828e"
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 2
                                    Label {
                                        Layout.fillWidth: true; elide: Text.ElideRight; color: "white"; font.bold: true
                                        text: (deviceCard.modelData.channel ? "CH" + deviceCard.modelData.channel + " · " : "") + deviceCard.modelData.name
                                    }
                                    Label {
                                        Layout.fillWidth: true; elide: Text.ElideRight; color: "#9eb0bd"
                                        text: deviceCard.modelData.id + " · " + (deviceCard.modelData.connected ? "Verbunden" : "Offline")
                                              + (deviceCard.modelData.saved ? " · Gespeichert" : "")
                                    }
                                }
                                ComboBox {
                                    id: colorBox
                                    Accessible.name: "Gerätefarbe"
                                    model: root.deviceColorNames
                                    currentIndex: root.deviceColors.indexOf(deviceCard.modelData.color || "")
                                    displayText: currentIndex < 0 ? "Farbe wählen" : currentText
                                }
                                Button {
                                    text: "Speichern"
                                    enabled: colorBox.currentIndex >= 0
                                    onClicked: pokit.saveDeviceColor(deviceCard.modelData.id, root.deviceColors[colorBox.currentIndex])
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
