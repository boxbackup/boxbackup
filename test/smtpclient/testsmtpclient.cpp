// --------------------------------------------------------------------------
//
// File
//		Name:    testsmtpclient.cpp
//		Purpose: Test SMTPClient class
//		Created: 28/10/04
//
// --------------------------------------------------------------------------

#include "Box.h"

#include <string.h>

#include "Test.h"
#include "SMTPClient.h"
#include "autogen_SMTPClientException.h"
#include "MemBlockStream.h"
#include "ValidateEmailAddress.h"

#include "MemLeakFindOn.h"

// validation of emails
typedef struct
{
	const char *input;
	bool lookup;
	bool expected_result;
	const char *expected_output;
} email_valid_test_t;

email_valid_test_t email_valid_test[] = 
{
	{"ben@example.co.uk", true, true, "ben@example.co.uk"},
	{" ben @ example.c o.uk ", true, true, "ben@example.co.uk"},
	{" ben@examplecouk", true, false, "ben@examplecouk"},
	{"BEN@FLUFFY.co.uk", true, true, "BEN@example.co.uk"},
	{"ben@example.-co.uk", false, false, "ben@example.-co.uk"},
	{"ben@example@co.uk", false, false, "ben@example@co.uk"},
	{"@example.co.uk", false, false, "@example.co.uk"},
	{"ben@example.a-co.uk", false, true, "ben@example.a-co.uk"},
	{"ben@example.*co.uk", false, false, "ben@example.*co.uk"},
	{"ben@example,co.uk ", false, false, "ben@example,co.uk"},
	{"ben@. ", false, false, "ben@"},
	{"ben@example.nosuchtld", false, true, "ben@example.nosuchtld"},
	{"ben@example.nosuchtld", true, false, "ben@example.nosuchtld"},
	{"ben@ relay.example.co.uk", true, true, "ben@relay.example.co.uk"},
	{0, false, false, 0}
};


int test(int argc, const char *argv[])
{
	const static char *message = 
		"To: ben.summers@example.co.uk\n"\
		"From: ben@example.co.uk\n"\
		"Subject: Test email\n\n"\
		"This is a test email.\n\n"\
		"\r\n\r\n.\r\nLine after . on it's own\r\n"\
		".Something\r\nEnd of message\n";

	// Don't send lots of emails in automated test runs!
	if(argc == 2 && ::strcmp(argv[1], "send") == 0)
	{
		SMTPClient smtp("INSERT-RELAY-HERE", "INSERT-HOSTNAME-HERE");
		SMTPClient::SendEmail send(smtp, "ben@example.co.uk");
		send.To("ben.summers@example.co.uk");
		MemBlockStream messageStream(message, strlen(message));
		send.Message(messageStream);
	
		SMTPClient::SendEmail send2(smtp, "ben@example.co.uk");
		send2.To(std::string("ben.summers@example.co.uk"));
		send2.To(std::string("postmaster@example.co.uk"));
		MemBlockStream messageStream2(message, strlen(message));
		send2.Message(messageStream2);
	}
	if(argc == 2 && ::strcmp(argv[1], "send2") == 0)
	{
		SMTPClient smtp("INSERT-RELAY-HERE", "INSERT-HOSTNAME-HERE");
		smtp.SendMessage(std::string("ben@example.co.uk"),
			std::string("ben.summers@example.co.uk"), std::string(message));
	}
	
	// Email validity test
	email_valid_test_t *evt = email_valid_test;
	while(evt->input != 0)
	{
		std::string in(evt->input), out;
		int res = ValidateEmailAddress(in, out, evt->lookup);
		TRACE5("'%s' -> '%s', lookup = %s, valid = %s (expected = %s)\n",
			in.c_str(), out.c_str(), (evt->lookup)?"true":"false", (res)?"true":"false", (evt->expected_result)?"true":"false");
		TEST_THAT(res == evt->expected_result);
		TEST_THAT(::strcmp(out.c_str(), evt->expected_output) == 0);

		// Next test
		++evt;
	}
	
	return 0;
}

