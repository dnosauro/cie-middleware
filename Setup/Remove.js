var progPath = Session.Property("CustomActionData");var fso = new ActiveXObject("Scripting.FileSystemObject");var path=fso.BuildPath(progPath,"Middleware CIE\\Cambio PIN.lnk");var path2=fso.BuildPath(progPath,"Middleware CIE\\Sblocco PIN.lnk");var path3=fso.BuildPath(progPath,"Middleware CIE");try {	fso.DeleteFile(path,true);}catch (x) {}try {	fso.DeleteFile(path2,true);}catch (x) {}try {	fso.DeleteFolder(path3,true);}catch (x) {}