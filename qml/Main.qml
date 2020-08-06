import Felgo 3.0
import QtQuick 2.12
import QtQuick.Controls 2.12
import QtMultimedia 5.13
import CVFilter 1.0

App {
    width: 900
    height: 600

    property bool drawing: false

    property var colors: ["#922040","#517E7E","#DC6E4F","#63BDCF","#EFC849","#1D2326","#301E34","#644D52","#55626F",
        "#35558A","#3190BB","#66D2D5","#BBE4E5","#AED8C7","#7ACBA5","#FEEFA9","#FFFEA2","#FB4668",
        "#FD6769","#FD9A9B","#FED297","#E6E1B1","#D1DABE","#E7E8D2","#CBDAEC","#D6D0F0","#CECECE",
        "#ECF0F1","#00887A","#86B2A5"]

    function resetBoundingBoxes() {
        for (var i = 0; i < boundingBoxesHolder.count; ++i)
            boundingBoxesHolder.itemAt(i).visible = false;
    }

    Timer{
        id: drawingTimer
        interval: 100
        onTriggered: {
            drawing = false;
        }
    }

    Camera {
        id: camera
        position: Camera.FrontFace
        viewfinder {
            //resolution: "320x240"
            maximumFrameRate: 15
        }
    }

    CVFilter{
        id: cvFilter

        onObjectsDetected: {

            if(drawing) return;

            drawing = true;

            resetBoundingBoxes();

            rects = JSON.parse(rects);

            var contentRect = output.contentRect;

            for(let i = 0; i < rects.length; i++){

                if(i > boundingBoxesHolder.count-1){
                    break;
                }

                var boundingBox = boundingBoxesHolder.itemAt(i);

                var r = {
                    x: rects[i].rX * contentRect.width,
                    y: rects[i].rY * contentRect.height,
                    width: rects[i].rWidth * contentRect.width,
                    height: rects[i].rHeight * contentRect.height
                };

                boundingBox.className = rects[i].className;
                boundingBox.x = r.x;
                boundingBox.y = r.y;
                boundingBox.width = r.width;
                boundingBox.height = r.height;
                boundingBox.visible = true;
            }

            drawingTimer.start();

        }
    }

    VideoOutput {
        id: output
        source: camera
        anchors.fill: parent
        focus : visible
        fillMode: VideoOutput.PreserveAspectCrop
        filters: [cvFilter]
        autoOrientation: true

        //bounding boxes parent
        Item {
            width: output.contentRect.width
            height: output.contentRect.height
            anchors.centerIn: parent

            Repeater{
                id: boundingBoxesHolder
                model: 20

                Rectangle{
                    border.width: 2
                    border.color: colors[index]
                    visible: false
                    color: "transparent"

                    property string className: ""

                    Rectangle{
                        width: childrenRect.width
                        height: 20
                        color: "white"
                        anchors.bottom: parent.top
                        anchors.left: parent.left

                        Text {
                            text: className
                            font.pixelSize: 15
                            width: contentWidth + leftPadding + 10
                            height: parent.height
                            leftPadding: 10
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                //end of Repeater
            }

        }

        Button{
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            width: 120
            height: 40
            text: "Toggle"
            onClicked: {

                if(QtMultimedia.availableCameras.length>1){

                    if(camera.position === Camera.BackFace){
                        camera.position = Camera.FrontFace;
                    }else{
                        camera.position = Camera.BackFace;
                    }
                }
            }
        }
    }

}
