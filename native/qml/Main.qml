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
    property real voltsDiv: pokit.scopeVoltsPerDiv
    property int timeDivUs: pokit.scopeTimePerDivUs
    property real verticalPosition: 0.0
    property real triggerPosition: 0.5
    property bool timeCursorsVisible: true
    property bool voltageCursorsVisible: true
    property real cursorT1: 0.30
    property real cursorT2: 0.70
    property real cursorU1: 0.30
    property real cursorU2: 0.70
    readonly property real cursorWindowUs: pokit.scopeRfPair ? pokit.scopeRfWindowUs : timeDivUs * 10
    readonly property real cursorTriggerFraction: pokit.scopeRfPair ? pokit.scopeRfTriggerFraction : triggerPosition
    readonly property real cursorDeltaUs: (cursorT2 - cursorT1) * cursorWindowUs
    readonly property real cursorDeltaV: (cursorU1 - cursorU2) * voltsDiv * 8

    function cursorTime(us) {
        const magnitude = Math.abs(us)
        if (magnitude < 1000) return us.toFixed(2) + " µs"
        if (magnitude < 1000000) return (us / 1000).toFixed(3) + " ms"
        return (us / 1000000).toFixed(4) + " s"
    }
    function cursorVoltage(v) { return Math.abs(v) < 1 ? (v * 1000).toFixed(2) + " mV" : v.toFixed(3) + " V" }
    function cursorFrequency() {
        if (Math.abs(cursorDeltaUs) < 0.000001) return "—"
        const hz = 1000000 / Math.abs(cursorDeltaUs)
        return hz >= 1000 ? (hz / 1000).toFixed(3) + " kHz" : hz.toFixed(3) + " Hz"
    }
    property var timeBases: [10,20,50,100,200,500,1000,2000,5000,10000,20000,50000,100000,200000,500000,1000000]

    function engineeringTime(us) { if(us<1000)return us+" µs/div";if(us<1000000)return(us/1000)+" ms/div";return(us/1000000)+" s/div" }
    function engineeringVoltage(v) { return v<1?Math.round(v*1000)+" mV/div":v+" V/div" }
    function setTimebaseIndex(index) { let i=Math.max(0,Math.min(timeBases.length-1,index));pokit.setScopeConfig(coupling.currentText,root.voltsDiv,timeBases[i]) }
    function metricTime(seconds) { if(seconds<=0)return "—";if(seconds<.001)return(seconds*1000000).toFixed(1)+" µs";if(seconds<1)return(seconds*1000).toFixed(2)+" ms";return seconds.toFixed(2)+" s" }

    component ScopeMetric: Column {
        required property string caption
        required property string reading
        Layout.fillWidth:true
        Layout.minimumWidth:0
        Layout.preferredWidth:100
        spacing:2
        Label { width:parent.width;text:parent.caption;color:"#9aafbd";font.pixelSize:11;elide:Text.ElideRight }
        Label { width:parent.width;text:parent.reading;color:"#edf6fb";font.family:"Consolas";font.pixelSize:15;font.bold:true;elide:Text.ElideRight }
    }

    component ChannelMetrics: Rectangle {
        id:metrics
        required property int channel
        required property bool available
        required property color channelColor
        property bool hasData:available&&(channel===1?pokit.scopeSamples.length>1:channel===2?pokit.scopeSamples2.length>1:false)
        readonly property real vpp:channel===1?pokit.scopeVpp:channel===2?pokit.scopeVpp2:0
        readonly property real frequency:channel===1?pokit.scopeFrequency:channel===2?pokit.scopeFrequency2:0
        readonly property real period:channel===1?pokit.scopePeriod:channel===2?pokit.scopePeriod2:0
        readonly property real cycles:channel===1?pokit.scopeCycles:channel===2?pokit.scopeCycles2:0
        readonly property real rms:channel===1?pokit.scopeRms:channel===2?pokit.scopeRms2:0
        readonly property real minimum:channel===1?pokit.scopeMinimum:channel===2?pokit.scopeMinimum2:0
        readonly property real maximum:channel===1?pokit.scopeMaximum:channel===2?pokit.scopeMaximum2:0
        Layout.fillWidth:true;Layout.minimumWidth:0;Layout.preferredWidth:1;Layout.preferredHeight:94
        radius:5;color:available?"#17232d":"#131c24";border.color:available?"#3d5260":"#293741"
        Rectangle { width:3;anchors.left:parent.left;anchors.top:parent.top;anchors.bottom:parent.bottom;anchors.margins:1;color:metrics.channelColor;opacity:metrics.available?1:.35 }
        GridLayout { anchors.fill:parent;anchors.margins:9;columns:4;columnSpacing:12;rowSpacing:8
            Column { Layout.fillWidth:true;Layout.minimumWidth:0;Layout.preferredWidth:100;spacing:2
                Label { text:"CH"+metrics.channel;color:metrics.channelColor;font.pixelSize:17;font.bold:true }
                Label { width:parent.width;text:metrics.hasData?"Messwerte":metrics.available?"Warte auf Daten":"Nicht verbunden";color:"#9aafbd";font.pixelSize:11;elide:Text.ElideRight }
            }
            ScopeMetric { caption:"Spitze–Spitze";reading:metrics.hasData?metrics.vpp.toFixed(3)+" V":"—" }
            ScopeMetric { caption:"Frequenz";reading:metrics.hasData&&metrics.frequency>0?(metrics.frequency>=1000?(metrics.frequency/1000).toFixed(2)+" kHz":metrics.frequency.toFixed(1)+" Hz"):"—" }
            ScopeMetric { caption:"Periodendauer";reading:metrics.hasData?root.metricTime(metrics.period):"—" }
            ScopeMetric { caption:"RMS";reading:metrics.hasData?metrics.rms.toFixed(3)+" V":"—" }
            ScopeMetric { caption:"Minimum";reading:metrics.hasData?metrics.minimum.toFixed(3)+" V":"—" }
            ScopeMetric { caption:"Maximum";reading:metrics.hasData?metrics.maximum.toFixed(3)+" V":"—" }
            ScopeMetric { caption:"Zyklen";reading:metrics.hasData?metrics.cycles.toFixed(0):"—" }
        }
    }

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
        }
        GridLayout { visible:!root.showDevices&&pokit.activeFeature==="scope";Layout.fillWidth:true;columns:2;columnSpacing:8;rowSpacing:6
            ChannelMetrics { channel:1;available:pokit.connected;channelColor:root.ch1 }
            ChannelMetrics { channel:2;available:pokit.connected2;channelColor:root.ch2 }
            ChannelMetrics { channel:3;available:false;channelColor:"#ffd629" }
            ChannelMetrics { channel:4;available:false;channelColor:"#55df89" }
        }
        StackLayout { currentIndex:root.showDevices?2:(pokit.activeFeature==="meter"?0:1);Layout.fillWidth:true;Layout.fillHeight:true
            MeterWorkspace { ch1:root.ch1;ch2:root.ch2;modes:root.meterModes }

            Rectangle { color:"#b8b5a9";radius:8;border.color:"#67675f";border.width:2
                RowLayout { anchors.fill:parent;anchors.margins:10;spacing:10
                    Rectangle { Layout.fillWidth:true;Layout.fillHeight:true;color:"#090d0b";border.color:"#525d54";border.width:5;radius:5
                        ColumnLayout { anchors.fill:parent;anchors.margins:7;spacing:3
                            RowLayout { Layout.fillWidth:true
                                Label {
                                    visible: pokit.connected2 || pokit.scopeTriggerSource > 0
                                    text: pokit.scopeRfPair ? "FUNK · CH" + pokit.scopeTriggerSource + " · Aufnahmepaar"
                                          : pokit.scopeTriggerSource > 0 ? "FUNK · CH" + pokit.scopeTriggerSource
                                          : "Kanäle unabhängig"
                                    color: pokit.scopeRfPair ? "#7eff8c" : "#b9c6bc"
                                }
                                Item { Layout.fillWidth:true }
                                CheckBox { text:"Zeitlinien";checked:root.timeCursorsVisible;onToggled:root.timeCursorsVisible=checked;palette.windowText:"#8de5ff" }
                                CheckBox { text:"Spannungslinien";checked:root.voltageCursorsVisible;onToggled:root.voltageCursorsVisible=checked;palette.windowText:"#ffcf82" }
                                Button { text:"↺";implicitWidth:32;ToolTip.visible:hovered;ToolTip.text:"Messlinien zurücksetzen";onClicked:{root.cursorT1=.3;root.cursorT2=.7;root.cursorU1=.3;root.cursorU2=.7} }
                                Label { text:pokit.scopeRunning?"● ACQUIRE":"■ STOP";color:pokit.scopeRunning?"#7eff8c":"#ffb56b";font.bold:true }
                                Label { text:pokit.scopeSampleRate>0?Math.round(pokit.scopeSampleRate/1000)+" kS/s":"— kS/s";color:"#b9c6bc" }
                            }
                            Canvas { id:scopeCanvas;Layout.fillWidth:true;Layout.fillHeight:true
                                Connections { target:pokit
                                    function onScopeSamplesChanged(){scopeCanvas.requestPaint()}
                                    function onReferenceChanged(){scopeCanvas.requestPaint()}
                                }
                                Connections { target:root;function onCh1Changed(){scopeCanvas.requestPaint()} function onCh2Changed(){scopeCanvas.requestPaint()} function onVoltsDivChanged(){scopeCanvas.requestPaint()} function onTimeDivUsChanged(){scopeCanvas.requestPaint()} }
                                onWidthChanged:requestPaint();onHeightChanged:requestPaint()
                                onPaint:{
                                    const paintStarted = Date.now()
                                    let c=getContext("2d"),a=pokit.scopeSamples,b=pokit.scopeSamples2;c.reset();c.fillStyle="#07100b";c.fillRect(0,0,width,height);c.strokeStyle="#294a34";c.lineWidth=1
                                    for(let i=0;i<=10;i++){let x=i*width/10;c.beginPath();c.moveTo(x,0);c.lineTo(x,height);c.stroke();for(let m=1;m<5&&i<10;m++){let xx=x+m*width/50;c.fillStyle="#25402f";c.fillRect(xx,height/2-2,1,5)}}
                                    for(let k=0;k<=8;k++){let y=k*height/8;c.beginPath();c.moveTo(0,y);c.lineTo(width,y);c.stroke();for(let n=1;n<5&&k<8;n++){let yy=y+n*height/40;c.fillStyle="#25402f";c.fillRect(width/2-2,yy,5,1)}}
                                    c.strokeStyle="#67806d";c.beginPath();c.moveTo(width/2,0);c.lineTo(width/2,height);c.stroke();c.beginPath();c.moveTo(0,height/2);c.lineTo(width,height/2);c.stroke()
                                    const triggerFraction = pokit.scopeRfPair ? pokit.scopeRfTriggerFraction : root.triggerPosition
                                    let tx=triggerFraction*width;c.fillStyle="#ff8c42";c.beginPath();c.moveTo(tx-7,0);c.lineTo(tx+7,0);c.lineTo(tx,10);c.closePath();c.fill()
                                    let zero=height/2-root.verticalPosition/root.voltsDiv*height/8;c.fillStyle=root.ch1;c.beginPath();c.moveTo(0,zero);c.lineTo(10,zero-6);c.lineTo(10,zero+6);c.closePath();c.fill()
                                    let saved=pokit.referenceSamples
                                    const visibleWindowUs = pokit.scopeRfPair ? pokit.scopeRfWindowUs : root.timeDivUs * 10
                                    function trace(values, color, alpha, sampleRate, triggerSample, originX) {
                                        if (values.length < 2) return
                                        c.save()
                                        c.beginPath()
                                        c.rect(0, 0, width, height)
                                        c.clip()
                                        c.globalAlpha = alpha
                                        c.strokeStyle = color
                                        c.lineWidth = 2
                                        c.lineJoin = "bevel"
                                        c.lineCap = "butt"
                                        c.beginPath()
                                        for (let i = 0; i < values.length; ++i) {
                                            const x = sampleRate > 0
                                                ? originX + (i - triggerSample) * width * 1e6 / (sampleRate * visibleWindowUs)
                                                : i * width / (values.length - 1)
                                            const y = height / 2 - ((values[i] - root.verticalPosition) / root.voltsDiv) * height / 8
                                            if (i === 0) c.moveTo(x, y)
                                            else c.lineTo(x, y)
                                            // Bound rasterizer work for noisy, self-intersecting
                                            // waveforms while keeping adjacent segments connected.
                                            if (i > 0) {
                                                c.stroke()
                                                c.beginPath()
                                                c.moveTo(x, y)
                                            }
                                        }
                                        c.stroke()
                                        c.restore()
                                    }
                                    const alignReferenceTrigger = pokit.scopeRfPair && pokit.referenceHasTrigger
                                    trace(saved, root.reference, .85, pokit.referenceSampleRate,
                                          alignReferenceTrigger ? pokit.referenceTriggerSample : 0,
                                          alignReferenceTrigger ? tx : 0)
                                    trace(b, root.ch2, 1, pokit.scopeRfPair ? pokit.scopeSampleRate2 : 0, pokit.scopeRfTriggerSample2, tx)
                                    trace(a, root.ch1, 1, pokit.scopeRfPair ? pokit.scopeSampleRate : 0, pokit.scopeRfTriggerSample, tx)
                                    if (Date.now() - paintStarted > 100) console.warn("Scope paint took", Date.now() - paintStarted, "ms")
                                }
                                MouseArea { anchors.fill:parent;acceptedButtons:Qt.NoButton
                                    onWheel:function(wheel){root.setTimebaseIndex(timeBox.currentIndex+(wheel.angleDelta.y>0?-1:1));wheel.accepted=true}
                                }
                                // Separate scene items keep cursor dragging independent of BLE
                                // acquisition and avoid repainting the waveform on every move.
                                Item { anchors.fill:parent;clip:true
                                    Repeater { model:2
                                        Item {
                                            id:timeCursor
                                            required property int index
                                            readonly property real fraction: index===0?root.cursorT1:root.cursorT2
                                            visible:root.timeCursorsVisible
                                            x:fraction*parent.width-width/2
                                            width:22;height:parent.height
                                            Rectangle { anchors.horizontalCenter:parent.horizontalCenter;width:1;height:parent.height;color:"#8de5ff";opacity:.85 }
                                            Rectangle {
                                                x:Math.max(-timeCursor.x, Math.min(12, timeCursor.parent.width-timeCursor.x-width))
                                                y:timeCursor.index===0?8:40
                                                width:timeText.implicitWidth+12;height:26;radius:3;color:"#102831";border.color:"#8de5ff"
                                                Label { id:timeText;anchors.centerIn:parent;color:"#b8efff";font.pixelSize:12
                                                    text:"T"+(timeCursor.index+1)+"  "+root.cursorTime((timeCursor.fraction-root.cursorTriggerFraction)*root.cursorWindowUs)
                                                }
                                            }
                                            MouseArea { anchors.fill:parent;cursorShape:Qt.SizeHorCursor
                                                function moveCursor(mouse) {
                                                    const p=mapToItem(scopeCanvas,mouse.x,mouse.y)
                                                    const value=Math.max(0,Math.min(1,p.x/scopeCanvas.width))
                                                    if(timeCursor.index===0)root.cursorT1=value;else root.cursorT2=value
                                                }
                                                onPressed:function(mouse){moveCursor(mouse)}
                                                onPositionChanged:function(mouse){if(pressed)moveCursor(mouse)}
                                                onWheel:function(wheel){wheel.accepted=false}
                                            }
                                        }
                                    }
                                    Repeater { model:2
                                        Item {
                                            id:voltageCursor
                                            required property int index
                                            readonly property real fraction:index===0?root.cursorU1:root.cursorU2
                                            visible:root.voltageCursorsVisible
                                            y:fraction*parent.height-height/2
                                            width:parent.width;height:22
                                            Rectangle { anchors.verticalCenter:parent.verticalCenter;width:parent.width;height:1;color:"#ffcf82";opacity:.85 }
                                            Rectangle {
                                                x:Math.max(0,parent.width-width-8)
                                                y:Math.max(-voltageCursor.y, Math.min(-18, voltageCursor.parent.height-voltageCursor.y-height))
                                                width:voltageText.implicitWidth+12;height:26;radius:3;color:"#302418";border.color:"#ffcf82"
                                                Label { id:voltageText;anchors.centerIn:parent;color:"#ffe0b0";font.pixelSize:12
                                                    text:"U"+(voltageCursor.index+1)+"  "+root.cursorVoltage(root.verticalPosition+(0.5-voltageCursor.fraction)*root.voltsDiv*8)
                                                }
                                            }
                                            MouseArea { anchors.fill:parent;cursorShape:Qt.SizeVerCursor
                                                function moveCursor(mouse) {
                                                    const p=mapToItem(scopeCanvas,mouse.x,mouse.y)
                                                    const value=Math.max(0,Math.min(1,p.y/scopeCanvas.height))
                                                    if(voltageCursor.index===0)root.cursorU1=value;else root.cursorU2=value
                                                }
                                                onPressed:function(mouse){moveCursor(mouse)}
                                                onPositionChanged:function(mouse){if(pressed)moveCursor(mouse)}
                                                onWheel:function(wheel){wheel.accepted=false}
                                            }
                                        }
                                    }
                                    Rectangle {
                                        visible:root.timeCursorsVisible||root.voltageCursorsVisible
                                        anchors.left:parent.left;anchors.bottom:parent.bottom;anchors.margins:8
                                        width:Math.min(parent.width-16,cursorResult.implicitWidth+20);height:30;radius:4;color:"#e6111d22";border.color:"#536973"
                                        Label { id:cursorResult;anchors.fill:parent;anchors.margins:6;color:"#eef7fa";font.pixelSize:12;elide:Text.ElideRight
                                            text:(root.timeCursorsVisible?"Δt (T2−T1)  "+root.cursorTime(root.cursorDeltaUs)+"    1/|Δt|  "+root.cursorFrequency():"")
                                                +(root.timeCursorsVisible&&root.voltageCursorsVisible?"     ·     ":"")
                                                +(root.voltageCursorsVisible?"ΔU (U2−U1)  "+root.cursorVoltage(root.cursorDeltaV):"")
                                        }
                                    }
                                }
                            }
                            RowLayout { Layout.fillWidth:true
                                Label { text:"CH1";color:"#171300";font.bold:true;padding:4;background:Rectangle{color:root.ch1;radius:2} }
                                Label { text:root.engineeringVoltage(root.voltsDiv);color:root.ch1;font.bold:true }
                                Label { visible:pokit.connected2;text:"CH2";color:"#07100b";font.bold:true;padding:4;background:Rectangle{color:root.ch2;radius:2} }
                                Item { Layout.fillWidth:true }
                                Label { text:"M  "+root.engineeringTime(pokit.scopeRfPair ? pokit.scopeRfWindowUs / 10 : root.timeDivUs);color:"#d8e0d9";font.bold:true }
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
                                        RowLayout { Label{text:"Kopplung";Layout.fillWidth:true} ComboBox{id:coupling;model:["DC","AC"];currentIndex:pokit.scopeCoupling==="AC"?1:0;onActivated:pokit.setScopeConfig(currentText,root.voltsDiv,root.timeDivUs)} }
                                        RowLayout { Label{text:"Volt / Div";Layout.fillWidth:true} ComboBox{id:voltsBox;model:["10 mV","20 mV","50 mV","100 mV","200 mV","500 mV","1 V","2 V","5 V","10 V","20 V","50 V"];currentIndex:[.01,.02,.05,.1,.2,.5,1,2,5,10,20,50].indexOf(root.voltsDiv);onActivated:{let v=[.01,.02,.05,.1,.2,.5,1,2,5,10,20,50][currentIndex];pokit.setScopeConfig(coupling.currentText,v,root.timeDivUs)}} }
                                        Label { text:"Position  "+root.verticalPosition.toFixed(2)+" V" }
                                        Slider { id:verticalSlider;Layout.fillWidth:true;from:-4*root.voltsDiv;to:4*root.voltsDiv;value:0;onMoved:{root.verticalPosition=value;scopeCanvas.requestPaint()} }
                                    }
                                }
                                GroupBox { title:"HORIZONTAL";Layout.fillWidth:true
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { Label{text:"Zeit / Div";Layout.fillWidth:true} ComboBox{id:timeBox;model:["10 µs","20 µs","50 µs","100 µs","200 µs","500 µs","1 ms","2 ms","5 ms","10 ms","20 ms","50 ms","100 ms","200 ms","500 ms","1 s"];currentIndex:root.timeBases.indexOf(root.timeDivUs);onActivated:root.setTimebaseIndex(currentIndex)} }
                                        Label { text:"Triggerposition" }
                                        Slider { Layout.fillWidth:true;from:.1;to:.9;value:.5
                                            onMoved:{root.triggerPosition=value;pokit.setScopeTriggerPosition(value);scopeCanvas.requestPaint()}
                                        }
                                    }
                                }
                                GroupBox { title:"TRIGGER";Layout.fillWidth:true
                                    ColumnLayout { anchors.fill:parent
                                        RowLayout { Label{text:"Modus";Layout.fillWidth:true} ComboBox{id:triggerMode;model:["Auto / Sofort","Steigend","Fallend"];currentIndex:["free","rising","falling"].indexOf(pokit.scopeTriggerMode);displayText:currentIndex===0?(pokit.scopeTriggerSource>0?"Sofort":"Auto"):currentText;onActivated:pokit.setScopeTrigger(["free","rising","falling"][currentIndex],triggerLevel.value/100)} }
                                        RowLayout {
                                            Label { text:"Quelle"; Layout.fillWidth:true }
                                            ComboBox {
                                                model: ["Je Kanal", "CH1 · Funk", "CH2 · Funk"]
                                                currentIndex: pokit.scopeTriggerSource
                                                enabled: pokit.connected2 || pokit.scopeTriggerSource > 0
                                                onActivated: {
                                                    pokit.setScopeTriggerSource(currentIndex)
                                                }
                                            }
                                        }
                                        Label { text:"Pegel  "+(triggerLevel.value/100).toFixed(2)+" V" }
                                        Slider { id:triggerLevel;Layout.fillWidth:true;from:-400*root.voltsDiv;to:400*root.voltsDiv;value:pokit.scopeTriggerLevel*100;stepSize:1;onMoved:pokit.setScopeTrigger(["free","rising","falling"][triggerMode.currentIndex],value/100) }
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
                                        }
                                        RowLayout { Layout.fillWidth:true
                                            Button { text:"EINBLENDEN";Layout.fillWidth:true;enabled:captureBox.currentIndex>=0;onClicked:{let item=pokit.savedCaptures[captureBox.currentIndex];if(item)pokit.loadScopeCapture(item.path)} }
                                            Button { text:"AUSBLENDEN";Layout.fillWidth:true;enabled:pokit.referenceName.length>0;onClicked:pokit.clearScopeReference() }
                                        }
                                        Label { Layout.fillWidth:true;wrapMode:Text.WordWrap;font.pixelSize:10;color:"#5e676b";text:"PDF-Bericht, JSON-Aufnahme und CSV-Wertetabelle werden gemeinsam gespeichert." }
                                    }
                                }
                                RowLayout { Layout.fillWidth:true
                                    Button {
                                        text:pokit.scopeRunning?"STOP":"RUN"
                                        enabled: pokit.scopeTriggerSource === 0 || pokit.scopeRunning || pokit.scopeRfReady
                                        Layout.fillWidth:true
                                        onClicked:pokit.setScopeRunning(!pokit.scopeRunning)
                                    }
                                    Button {
                                        text:"SINGLE";Layout.fillWidth:true
                                        enabled: pokit.scopeTriggerSource === 0 || (pokit.scopeRfReady && !pokit.scopeRunning)
                                        onClicked:pokit.scopeSingle()
                                    }
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
