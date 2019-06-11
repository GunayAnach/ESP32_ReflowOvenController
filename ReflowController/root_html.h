const char ROOT_HTML[] PROGMEM = R"=====(
<html>
  <head>
    <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
    <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.4.1/jquery.min.js"></script>
    <script>
      google.charts.load('current', {
        'packages': ['line', 'corechart']
      });
      google.charts.setOnLoadCallback(drawChart);
      function drawChart() {
        var chartDiv = document.getElementById('chart_div');
        var button = document.getElementById('action');
        var chartdata;
        function initchartdata(){
            chartdata = new google.visualization.DataTable()
            chartdata.addColumn('number', 'Seconds');
            chartdata.addColumn({type: 'string', role: 'annotation'});
            chartdata.addColumn('number', "Temperatur");
            chartdata.addColumn('number', "Setpoint");
            chartdata.addColumn('number', "Power");
        }
        initchartdata();

        var classicOptions = {
          title: 'ReflowOven',
          // Gives each series an axis that matches the vAxes number below.
          series: {
            0: {
              targetAxisIndex: 0
            },
            1: {
              targetAxisIndex: 0
            },
            2: {
              targetAxisIndex: 1
            }
          },
          vAxes: {
            // Adds titles to each axis.
            0: {
              title: 'Temps (�C)',
              viewWindow: {
                max:300,
                min:0
              }
            },
            1: {
              title: 'Power (%)',
              viewWindow: {
                max:100,
                min:0
              }
            }
          },
          hAxis: {
              title: 'Time (s)',
              viewWindow: {
                  max: 100
              },
          },
            annotations: {
               alwaysOutside: true,
                 style: 'line',
                 highContrast: true,
                 textStyle: {
                      bold: true
                    }
            }
        };

        var classicChart = new google.visualization.LineChart(chartDiv);
        var startTime=Date.now();
        var lastState= "";
        var running=false;
        function loadstatus()
        {
          var lastcall=Date.now();
          $.getJSON( "status?t="+Date.now() )
           .done(function(data) {

             console.log( "success", data );

             if(data.state=="Idle")
             {
                 //clear all
                 initchartdata();
                 startTime=Date.now();
                 lastState="Idle";
                 button.innerText="Start Reflow";
                 running=false;
             }
             else{
                 if(data.state=="Complete"){
                    button.innerText="Reset";                   
                 }
                 else{
                    button.innerText="Cancel";
                 }
                 running=true;
             }

             var lable=null;
             if(data.state!=lastState)
             {
                lastState= data.state;
                lable=data.state;
             }
             

             if(data.state!="Idle" && data.state!="Complete"){
                 chartdata.addRow([data.time/1000, lable, data.temp, data.setpoint, data.power]);
                 classicOptions.hAxis.viewWindow.max=Math.max(100,Math.round(data.time/1000.0)+10);
             }
             if(data.state=="Idle")
             {
                 classicOptions.hAxis.viewWindow.max=100;
             }
             if(data.state=="Complete")
             {
                 classicOptions.hAxis.viewWindow.max=null;
             }
             

             classicChart.draw(chartdata, classicOptions);
           })
           .always(function() {
             setTimeout(loadstatus, Math.max(10,1000-(Date.now()-lastcall)));
           });

        }

        loadstatus();

        button.onclick = function(){
            $.ajax({
                url : "http://"+window.location.hostname+":8080/"+(running?"stop":"start")+"?t="+Date.now(),
                dataType: "text",
                success : function (data) {
                  console.log("sucess",data);
                    if(data!="OK")
                        alert("Controller not ready!");
                },
                error:function(a,b,c){
                  console.log("error",a,b,c);
                    alert("Communication error!");
                }
            });
        }
          
      }
      
    </script>
  </head>
  <body>
    <div>        
        Action: &nbsp;&nbsp;&nbsp;<button id="action" style="position: absolute; z-index: 1000"> </button>
    </div>
    <div id="chart_div" style="width: 100%; height: 100%;"></div>
  </body>

</html>

)=====";
