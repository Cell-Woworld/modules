1. Copy Punch into [your_runtime_root/scxml/]
2. execute DB/schema/Punch.sql in the database or your project
3. Create Stored Procedure: invoke DB/SetupDB.scxml of Punch in the SetupDB.scxml of your project 
4. Initalize constant: change value of DataModel (DATABASE_NAME and PROJECT_NAME) to your project name in each scxml of Punch
5. add transition "Punch.Client.PunchSuccess" to YOUR_PROJECT_NAME_Websocket.scxml with calling script "Punch.Client.PunchSuccess"
6. Invoke Punch.scxml in your project flow