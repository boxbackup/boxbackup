Dim hostname
Dim account
Dim from
Dim sendto
Dim subjtmpl
Dim subject
Dim body
Dim smtpserver

Set WshNet = CreateObject("WScript.Network")
hostname = WshNet.ComputerName

account = "0x1"
from = "boxbackup@" & hostname
sendto = "admin@example.com"
smtpserver = "smtp.example.com"
subjtmpl = "BACKUP PROBLEM on host " & hostname

Set args = WScript.Arguments

If args(0) = "store-full" Then
	subject = subjtmpl & " (store full)"
	body =	"The store account for "&hostname&" is full." & vbCrLf & _
		vbCrLf & _
		"=============================" & vbCrLf & _
		"FILES ARE NOT BEING BACKED UP" & vbCrLf & _
		"=============================" & vbCrLf & _
		vbCrLf & _
		"Please adjust the limits on account "&account&" on server "&hostname&"." _
		& vbCrLf
	SendMail from,sendto,subject,body
ElseIf args(0) = "read-error" Then
	subject = subjtmpl & " (read errors)"
	body =	"Errors occurred reading some files or directories " & _
		"for backup on " & hostname & "." & vbCrLf & _
		vbCrLf & _
		"===================================" & vbCrLf & _
		"THESE FILES ARE NOT BEING BACKED UP" & vbCrLf & _
		"===================================" & vbCrLf & vbCrLf & _
		"Check the logs on "&hostname&" for the files and " & _
		"directories which caused" & vbCrLf & _
		"these errors, and take appropriate action." & vbCrLf & _
		vbCrLf & _
		"Other files are being backed up." & vbCrLf
	SendMail from,sendto,subject,body
ElseIf args(0) = "backup-error" Then
	subject = subjtmpl & " (read errors)"
	body =	"An error occurred during the backup on "&hostname&"." _
		& vbCrLf & vbCrLf & _
		"==========================" & vbCrLf & _
		"FILES MAY NOT BE BACKED UP" & vbCrLf & _
		"==========================" & vbCrLf & _
		vbCrLf & _
		"Check the logs on "&hostname&" for more " & _
		"information about the error, " & vbCrLf & _
		"and take appropriate action." & vbCrLf
	SendMail from,sendto,subject,body
ElseIf args(0) = "backup-start" Or args(0) = "backup-finish" _
	Or args(0) = "backup-ok" Then
	' do nothing for these messages by default
Else
	subject = subjtmpl & " (unknown)"
	body =	"The backup daemon on "&hostname&" reported an unknown error." _
		& vbCrLf & vbCrLf & _
		"==========================" & vbCrLf & _
		"FILES MAY NOT BE BACKED UP" & vbCrLf & _
		"==========================" & vbCrLf & vbCrLf & _
		"Please check the logs on "&hostname&"." & vbCrLf
	SendMail from,sendto,subject,body
End If

Function CheckSMTPSvc()
	Set objWMISvc = GetObject("winmgmts:" _
		& "{impersonationLevel=impersonate}!\\.\root\cimv2")
	Set colSMTPSvc = objWMISvc.ExecQuery("Select * From Win32_Service " _
		& "Where Name='SMTPSVC'")
	If colSMTPSvc.Count > 0 Then
		CheckSMTPSvc = True
	Else
		CheckSMTPSvc = False
	End If
End Function

Sub SendMail(from,sendto,subject,body)
	Set objEmail = CreateObject("CDO.Message")
	Set WshShell = CreateObject("WScript.Shell")
	Dim cdoschema
	cdoschema = "http://schemas.microsoft.com/cdo/configuration/"
	
	With objEmail
		.From = from
		.To = sendto
		.Subject = subject
		.TextBody = body
		If CheckSMTPSvc = False Then
			.Configuration.Fields.Item(cdoschema & "sendusing") = 2
			.Configuration.Fields.Item(cdoschema & "smtpserver") = smtpserver
			.Configuration.Fields.Item(cdoschema & "smtpserverport") = 25
			.Configuration.Fields.Update
		End If
	End With
	On Error Resume Next
	rc = objEmail.Send
	If rc Then
		WshShell.Exec "eventcreate /L Application /ID 201 /T WARNING " _
			& "/SO ""Box Backup"" /D """ & args(0) _
			& " notification sent to " & sendto & "."""
	Else
		WshShell.Exec "eventcreate /L Application /ID 202 /T ERROR " _
			& "/SO ""Box Backup"" /D ""Failed to send " & args(0) _
			& " notification to " & sendto & "."""
	End If
End Sub
