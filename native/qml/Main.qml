import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1360; height: 860; minimumWidth: 1080; minimumHeight: 700
    visible: true; title: "PokitSuite"; color: "#111820"
    property color panel: "#17212b"
    property color line: "#34424d"
    property color ch1: "#ffd629"
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
            Label { Layout.preferredWidth:190;horizontalAlignment:Text.AlignRight;elide:Text.ElideRight;text:pokit.connected?"●  "+pokit.deviceName:"○  Nicht verbunden";color:pokit.connected?"#4ee6a5":"#91a0aa" }
            Button { Layout.preferredWidth:125;text:pokit.connected?"Verbunden":"Verbinden";onClicked:pokit.connectDevice() }
        }
    }

    ColumnLayout { anchors.fill:parent;anchors.margins:12;spacing:8
        RowLayout { Layout.fillWidth:true;spacing:6
            Button { text:"Multimeter";highlighted:pokit.activeFeature==="meter";onClicked:pokit.showMultimeter() }
            Button { text:"Oszilloskop";highlighted:pokit.activeFeature==="scope";onClicked:pokit.showScope() }
            Item { Layout.fillWidth:true }
            Label { visible:pokit.activeFeature==="scope";text:"Vpp  "+pokit.scopeVpp.toFixed(3)+" V";color:root.ch1;font.bold:true }
            Label { visible:pokit.activeFeature==="scope";text:"f  "+(pokit.scopeFrequency>0?(pokit.scopeFrequency>=1000?(pokit.scopeFrequency/1000).toFixed(2)+" kHz":pokit.scopeFrequency.toFixed(1)+" Hz"):"—");color:"#dce5e9" }
            Label { visible:pokit.activeFeature==="scope";text:"T  "+root.metricTime(pokit.scopePeriod);color:"#dce5e9" }
            Label { visible:pokit.activeFeature==="scope";text:"Zyklen  "+pokit.scopeCycles.toFixed(0);color:"#dce5e9" }
            Label { visible:pokit.activeFeature==="scope";text:"RMS  "+pokit.scopeRms.toFixed(3)+" V";color:"#dce5e9" }
            Label { visible:pokit.activeFeature==="scope";text:"Min/Max  "+pokit.scopeMinimum.toFixed(2)+" / "+pokit.scopeMaximum.toFixed(2)+" V";color:"#dce5e9" }
        }
        StackLayout { currentIndex:pokit.activeFeature==="meter"?0:1;Layout.fillWidth:true;Layout.fillHeight:true
            Rectangle { color:root.panel;radius:12;border.color:root.line
                ColumnLayout { anchors.fill:parent;anchors.margins:24
                    RowLayout { Layout.fillWidth:true
                        Label { text:"LIVE-MULTIMETER";color:"#8ba0af";Layout.fillWidth:true }
                        ComboBox { model:["DC Voltage","AC Voltage","DC Current","AC Current"];onActivated:pokit.setMeterMode(currentText) }
                        ComboBox { model:["0,5 s","1 s","2 s"];onActivated:pokit.setMeterInterval([500,1000,2000][currentIndex]) }
                    }
                    Row { Layout.alignment:Qt.AlignHCenter;spacing:16
                        Label { text:Number(pokit.meterValue).toLocaleString(Qt.locale("de_DE"),'f',4);color:"#edf6fb";font.pixelSize:92 }
                        Label { text:pokit.meterUnit;color:"#93a9b8";font.pixelSize:24;anchors.baseline:parent.children[0].baseline }
                    }
                    Canvas { id:meterCanvas;Layout.fillWidth:true;Layout.fillHeight:true
                        Connections { target:pokit;function onMeterHistoryChanged(){meterCanvas.requestPaint()} }
                        onPaint:{let c=getContext("2d"),a=pokit.meterHistory;c.reset();c.strokeStyle="#2a3e4b";for(let i=0;i<5;i++){let y=i*height/4;c.beginPath();c.moveTo(0,y);c.lineTo(width,y);c.stroke()}if(a.length<2)return;let lo=Math.min(...a),hi=Math.max(...a);if(lo===hi){lo--;hi++}c.strokeStyle="#55e2b0";c.lineWidth=2;c.beginPath();for(let j=0;j<a.length;j++){let x=j*width/(a.length-1),y=(hi-a[j])*height/(hi-lo);j?c.lineTo(x,y):c.moveTo(x,y)}c.stroke()}
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
                                Connections { target:pokit;function onScopeSamplesChanged(){scopeCanvas.requestPaint()} }
                                onWidthChanged:requestPaint();onHeightChanged:requestPaint()
                                onPaint:{
                                    let c=getContext("2d"),a=pokit.scopeSamples;c.reset();c.fillStyle="#07100b";c.fillRect(0,0,width,height);c.strokeStyle="#294a34";c.lineWidth=1
                                    for(let i=0;i<=10;i++){let x=i*width/10;c.beginPath();c.moveTo(x,0);c.lineTo(x,height);c.stroke();for(let m=1;m<5&&i<10;m++){let xx=x+m*width/50;c.fillStyle="#25402f";c.fillRect(xx,height/2-2,1,5)}}
                                    for(let k=0;k<=8;k++){let y=k*height/8;c.beginPath();c.moveTo(0,y);c.lineTo(width,y);c.stroke();for(let n=1;n<5&&k<8;n++){let yy=y+n*height/40;c.fillStyle="#25402f";c.fillRect(width/2-2,yy,5,1)}}
                                    c.strokeStyle="#67806d";c.beginPath();c.moveTo(width/2,0);c.lineTo(width/2,height);c.stroke();c.beginPath();c.moveTo(0,height/2);c.lineTo(width,height/2);c.stroke()
                                    let tx=root.triggerPosition*width;c.fillStyle="#ff8c42";c.beginPath();c.moveTo(tx-7,0);c.lineTo(tx+7,0);c.lineTo(tx,10);c.closePath();c.fill()
                                    let zero=height/2-root.verticalPosition/root.voltsDiv*height/8;c.fillStyle=root.ch1;c.beginPath();c.moveTo(0,zero);c.lineTo(10,zero-6);c.lineTo(10,zero+6);c.closePath();c.fill()
                                    if(a.length<2)return;c.strokeStyle=root.ch1;c.lineWidth=2;c.beginPath();for(let j=0;j<a.length;j++){let x=j*width/(a.length-1),y=height/2-((a[j]-root.verticalPosition)/root.voltsDiv)*height/8;j?c.lineTo(x,y):c.moveTo(x,y)}c.stroke()
                                }
                                MouseArea { anchors.fill:parent;acceptedButtons:Qt.NoButton
                                    onWheel:function(wheel){root.setTimebaseIndex(timeBox.currentIndex+(wheel.angleDelta.y>0?-1:1));wheel.accepted=true}
                                }
                            }
                            RowLayout { Layout.fillWidth:true
                                Label { text:"CH1";color:"#171300";font.bold:true;padding:4;background:Rectangle{color:root.ch1;radius:2} }
                                Label { text:root.engineeringVoltage(root.voltsDiv);color:root.ch1;font.bold:true }
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
                                RowLayout { Repeater { model:4;Button { required property int index;text:"CH"+(index+1);enabled:index===0;highlighted:index===0;Layout.fillWidth:true } } }
                                GroupBox { title:"VERTICAL · CH1";Layout.fillWidth:true
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
                                        RowLayout { Label{text:"Quelle";Layout.fillWidth:true} ComboBox{model:["CH1","CH2","CH3","CH4"];enabled:false} }
                                        Label { text:"Pegel  "+(triggerLevel.value/100).toFixed(2)+" V" }
                                        Slider { id:triggerLevel;Layout.fillWidth:true;from:-400;to:400;value:0;stepSize:1;onMoved:pokit.setScopeTrigger(["free","rising","falling"][triggerMode.currentIndex],value/100) }
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
        }
    }
}
