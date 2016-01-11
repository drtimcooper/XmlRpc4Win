#include <iostream>
#include <time.h>
#include "PlainXmlRpc.h"


// main.cpp:   includes both a little unit test and a sample client program.

using namespace PlainXML;


static void BasicTest()
{
	// Replace this URL with your own URL:
	XmlClient Connection("https://61.95.191.232:9600/arumate/rpc/xmlRpcServer.php");
				// Does anyone know of an XmlRpc server that I could insert here 
				// as a test machine that's always available?
	Connection.setIgnoreCertificateAuthority();

	//  Call:  arumate.getKilowatts(string, integer, boolean)   :
	XmlArray args;
	args[0] = RpcVal("test");
	args[1] = RpcVal(1);
	args[2] = RpcVal(3.141);
	XmlValue* result;

	// Replace this function name with your own:
	result = Connection.execute("arumate.getKilowatts", &args);
	if (result == NULL) {
		std::cout << Connection.getError();
	}
	else if (result->type() == TypeString)
		std::cout << ((XmlString*)result)->stri;
	else std::cout << "Success\n";
}


static void AdvancedTest()
{
	XmlArray args("params");
	XmlValue *result;

	// Passing datums:
	args[0] = new XmlString("a string");
	args[1] = new XmlInt(1);
	args[2] = new XmlBool(true);
	args[3] = new XmlDouble(3.14159);
	time_t time1 = time(NULL);
	struct tm* timeNow = localtime(&time1);
	args[4] = new XmlDateTime(timeNow);

	// Passing an array:
	XmlArray array;
	array[0] = new XmlInt(4);
	array[1] = new XmlInt(5);
	array[2] = new XmlDouble(6.5);
	/* Or:*/
	array.add(4);
	array.add(5);
	array.add(6.5);
	/**/
	args[5] = &array;

	// Passing a struct:
	XmlArray *record = new XmlArray;
	record->set("SOURCE", "a");
	record->set("DESTINATION", "b");
	record->set("LENGTH", 5);
	args[6] = record;

	// What does it look like as XML?
	std::ostringstream ostr;
	args.toXml(ostr);
	kstr xml = strdup(ostr.str().c_str());
	puts(xml);

	// Can we parse it back to C++ objects?
	XmlArray &echo = *(XmlArray*)ValueFromXml(xml);
	kstr p0 = *echo[0];
	int p1 = *echo[1];
	bool p2 = *echo[2];
	double p3 = *echo[3];
	struct tm p4;
	echo[4]->asTmStruct(&p4);
	time_t tmp = mktime(&p4);
	printf("\"%s\", %d, %s, %1.4f, {%s}\n", p0, p1, (p2?"true":"false"), p3, ctime(&tmp));

	// Make the call:
	XmlClient Connection("http://localhost:9090/timetable/rpc");
	Connection.setIgnoreCertificateAuthority();
	result = Connection.execute("getKilowatts", &args);
	if (result == NULL) {
		std::cout << Connection.getError();
		return;
	}

	// Pull the data out:
	if (result->type() != TypeArray) {
		std::cout << "I was expecting an array.";
		return;
	}
	{	double wattage;
		int stationId;
		bool threePhase;
		struct tm shutdown;

		XmlArray &r = *(XmlArray*)result;
		if (r.find("wattage"))
			wattage = *r["wattage"];
		if (r.find("threePhase"))
			threePhase = *r["threePhase"];
		if (r.find("stationId"))
			stationId = *r["stationId"];
		if (r.find("shutdown"))
			(*r["shutdown"]).asTmStruct(&shutdown);
		time_t tmp = mktime(&shutdown);
		printf("%1.4f  %s  %d  %s\n", wattage, (threePhase?"Y":"N"), stationId, ctime(&tmp));
		if (r.find("servers")) {
			XmlArray &servers = *(XmlArray*)r["servers"];
			for (int i=0; i < servers.size(); i++) {
				XmlValue *server = servers[i];
				printf("%s\n", (kstr)*server);
			}
		}
	}
}


//---------------------------- main(): -------------------------

int main(int argc, char* argv[])
{
	//BasicTest();
	AdvancedTest();
	return 0;
}

