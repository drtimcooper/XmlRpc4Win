
#undef UNICODE
#define _SECURE_SCL		0			// Disable "checked iterators".  STL is too slow otherwise.
#include <windows.h>
#include <wininet.h>
#include <fstream>
#include "PlainXmlRpc.h"





#define not		!
#define	or		||
#define and		&&
extern int L;
extern void stop();



//---------------------------------- Misc: -------------------------------

static bool strieq(kstr a, kstr b)
{
	return _stricmp(a, b) == 0;
}


static bool strbegins(kstr bigstr, kstr smallstr, bool casesensitive)
{
    kstr smalls = smallstr;
    kstr bigs = bigstr;
    while (*smalls) {
        if (casesensitive) {
            if (*bigs != *smalls)
                return false;
            else bigs++, smalls++;
        }
        else {
            if (toupper(*bigs) != toupper(*smalls))
                return false;
            else bigs++, smalls++;
        }
    }
    return true;
}


static void assert_failed(kstr filename, int line, kstr condition)
{
	printf("Assert failed|   %s:%d   %s\n", filename, line, condition);
}

#define assert(c)		if (c) ; else assert_failed(__FILE__, __LINE__, #c)



namespace PlainXML {

/*----------------------------------- Generic functions: ------------------------------------*/

XmlValue::~XmlValue()
{
	if (nameIsOwned)
		free(name);
	name = NULL;
}


// Map something like:    "T2-D&amp;T1"  to  "T2-D&T1"
// Returns a 'strdup()' version of the input string.
static str xmlDecode(kstr s, kstr end)
{
	str dest = (char*)malloc(end - s + 1);
	str d = dest;
	while (s < end) {
		if (*s != '&')
			*d++ = *s++;
		else if (strbegins(s, "&amp;", true))
			*d++ = '&', s += 5;
		else if (strbegins(s, "&quot;", true))
			*d++ = '\"', s += 6;
		else if (strbegins(s, "&apos;", true)/*not standard*/ or strbegins(s, "&#039;", true))
			*d++ = '\'', s += 6;
		else if (strbegins(s, "&lt;", true))
			*d++ = '<', s += 4;
		else if (strbegins(s, "&gt;", true))
			*d++ = '>', s += 4;
		else if (strbegins(s, "&#", true)) {
			s += 2;
			*d++ = atoi(s+2);
			while (*s >= '0' and *s <= '9')
				s++;
			if (*s == ';')
				s++;
		}
		else
			*d++ = *s++;	// assert(false);
	}
	*d = '\0';
	return dest;
}


// Replace raw text with xml-encoded entities.

static std::string xmlEncode(kstr s)
{
	std::ostringstream ostr;

	while (*s) {
		if (*s == '&')
			ostr << "&amp;";
		else if (*s == '<')
			ostr << "&lt;";
		else if (*s == '>')
			ostr << "&gt;";
		else if (*s == '"')
			ostr << "&quot;";
		else if (*s == '\'')
			ostr << "&apos;";// Would David prefer:  "&#039;" ?
		else if (*s < ' ' or *s >= 127)
			ostr << "&#" << int((unsigned char)*s) << ';';
		else ostr << *s;
		s++;
	}
	return ostr.str();
}


static void SkipWhiteSpace(kstr &s)
{
	while (*s == ' ' or *s == '\t' or *s == '\n' or *s == '\r')
		s++;
}


static char* GobbleTag(kstr &s, char dest[128])
// E.g. given:  "< string  / >",  return "<string/>"
{
	*dest = '\0';
	SkipWhiteSpace(s);
	if (*s != '<')
		return dest;
	str d = dest;
	*d++ = *s++;
	SkipWhiteSpace(s);
	while (*s and *s != '>') {
		if (*s == ' ' and (d[-1] == '<' or d[-1] == '/' or s[1] == ' ' or s[1] == '/' or s[1] == '>'))
			s++;
		else *d++ = *s++;
	}
	*d++ = '>';
	*d = '\0';
	if (*s == '>')
		s++;
	return dest;
}


static void GobbleExpectedTag(kstr &s, kstr etag)
{
	char tag[128];
	GobbleTag(s, tag);
	if (not strieq(tag, etag))
		throw XmlException(std::string("Expecting tag: ") + etag);
}


XmlValue* ValueFromXml(kstr &s)
{	XmlValue *value=NULL;
	str strdupName=NULL;
	char tag[128];
	
	GobbleTag(s, tag+1);

	// Set the name of the object:
	str name = tag + 2;
	str nameEnd = strchr(name, '>');
	if (nameEnd) {
		if (nameEnd[-1] == '/') {
			// It's a null element, e.g. 'member' within:   <fields><member/></fields>
			nameEnd[-1] = '\0';
			value = new XmlString("");
			value->setNameStdrup(name);
			return value;
		}
		else {
			*nameEnd = '\0';
			strdupName = strdup(name);
			*nameEnd = '>';
		}
	}

	// Parse the contents:
	SkipWhiteSpace(s);
	if (*s == '<' and s[1] == '/') {
		// This should be an error:  and end tag received when we're expecting a start tag.
		value = new XmlString("");
	}
	else if (*s == '<') {
		// Parse a structured value as an array.  If the caller wants it as a struct,
		// then he can call 'convertToStruct()' on it.
		XmlArray *array = new XmlArray;
		array->fromXml(s);
		value = array;
	}
	else {
		// Parse an atom, as a string:
		kstr valueEnd = strchr(s, '<');
		if (valueEnd == NULL)
			throw XmlException("The XML file is incomplete.");
		XmlString *stringValue = new XmlString(xmlDecode(s, valueEnd));
		s = valueEnd;
		value = stringValue;
	}
	*tag = '<';
	tag[1] = '/';
	GobbleExpectedTag(s, tag);
	value->setNameConst(strdupName);
	return value;
}


void XmlValue::toXml(std::ostringstream &ostr) const
{
	if (this == NULL)
		return;
	kstr tag = (name and *name) ? name : "value";
	ostr << '<' << tag << '>';
	toXmlInternal(ostr);
	ostr << "</" << tag << ">\n";
}


static bool SuitableForCDATA(str s)
{
	if (strchr(s, '<') and strchr(s, '>'))
		return true;
	return false;
}


XmlValue* RpcVal(int i, kstr name) { return new XmlInt(i, name); }
XmlValue* RpcVal(kstr s, kstr name) { return new XmlString(s, name); }
XmlValue* RpcVal(std::string s, kstr name) { return new XmlString(s.c_str(), name); }
XmlValue* RpcVal(double f, kstr name) { return new XmlDouble(f, name); }
XmlValue* RpcVal(bool b, kstr name) { return new XmlBool(b, name); }




//---------------------------------- XmlArray: -------------------------------

void XmlArray::resize(int n)
{
	if (n >= _allocated) {
		// Optimise growing of the array:
		int power2 = n - 1;
		power2 |= power2 >> 1;
		power2 |= power2 >> 2;
		power2 |= power2 >> 4;
		power2 |= power2 >> 8;
		power2 |= power2 >> 16;
		power2++;

		// Set the size:
		A = (XmlValue**)realloc(A, power2 * sizeof(XmlValue));
		memset(A + _allocated, 0, (power2 - _allocated) * sizeof(XmlValue));
		_allocated = power2;
	}
	_size = n;
}


bool XmlArray::operator==(XmlArray &other)
{
	if (_size != other._size)
		return false;
	for (int i=0; i < _size; i++) {
		if (A[i] == NULL or other.A[i] == NULL)
			return A[i] == other.A[i];
		if (A[i] != other.A[i])//Doesn't work yet.  This is only shallow equality.
			return false;
	}
	return true;
}


XmlArray::XmlArray(XmlArray &other)
{
	A = NULL; 
	_size = _allocated = 0;
	resize(other._size);
    for (int i=0 ; i < _size; i++) {
        A[i] = other.A[i];
    }
}


XmlValue* XmlArray::find(const char *s)
{
	for (int i=0; i < _size; i++) {
		if (strcmp(A[i]->getName(), s) == 0)
			return A[i];
	}
	return NULL;
}


void XmlArray::set(const char *s, XmlValue *value)
{
	value->setNameConst(s);
	for (int i=0; i < _size; i++) {
		if (strcmp(A[i]->getName(), s) == 0) {
			delete A[i];
			A[i] = value;
			return;
		}
	}
	int i = _size;
	resize(i+1);
	A[i] = value;
}


XmlArray::~XmlArray()
{
	for (int i=0; i < _size; i++) {
		if (A[i])
			delete A[i];
	}
	free(A);
}


void XmlArray::fromXml(kstr &s)
{
	do {
		SkipWhiteSpace(s);
		if (strbegins(s, "</", true))
			break;
		int n = _size;
		resize(n+1);
		A[n] = ValueFromXml(s);
	} while (true);
}


void XmlArray::toXmlInternal(std::ostringstream &ostr) const
{
	for (int i=0; i < _size; ++i) {
		if (A[i])
			A[i]->toXml(ostr);
	}
}




//---------------------------------- base64.h: -------------------------------
	/* <Chris Morley> */
static
int _base64Chars[]= {'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
						 'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
							 '0','1','2','3','4','5','6','7','8','9',
							 '+','/' };


#define _0000_0011 0x03
#define _1111_1100 0xFC
#define _1111_0000 0xF0
#define _0011_0000 0x30
#define _0011_1100 0x3C
#define _0000_1111 0x0F
#define _1100_0000 0xC0
#define _0011_1111 0x3F

#define _EQUAL_CHAR	 (-1)
#define _UNKNOWN_CHAR (-2)

#define _IOS_FAILBIT	 std::ios_base::failbit
#define _IOS_EOFBIT		std::ios_base::eofbit
#define _IOS_BADBIT		std::ios_base::badbit
#define _IOS_GOODBIT	 std::ios_base::goodbit

// TEMPLATE CLASS base64_put
class base64 {
public:

	typedef unsigned char byte_t;
	typedef char						char_type;
	typedef std::char_traits<char>					 traits_type; 

	// base64 requires max line length <= 72 characters
	// you can fill end of line
	// it may be crlf, crlfsp, noline or other class like it


	struct crlf
	{
		template<class _OI>
			_OI operator()(_OI _To) const{
			*_To = std::char_traits<char>::to_char_type('\r'); ++_To;
			*_To = std::char_traits<char>::to_char_type('\n'); ++_To;

			return (_To);
		}
	};


	struct crlfsp
	{
		template<class _OI>
			_OI operator()(_OI _To) const{
			*_To = std::char_traits<char>::to_char_type('\r'); ++_To;
			*_To = std::char_traits<char>::to_char_type('\n'); ++_To;
			*_To = std::char_traits<char>::to_char_type(' '); ++_To;

			return (_To);
		}
	};

	struct noline
	{
		template<class _OI>
			_OI operator()(_OI _To) const{
			return (_To);
		}
	};

	struct three2four
	{
		void zero()
		{
			_data[0] = 0;
			_data[1] = 0;
			_data[2] = 0;
		}

		byte_t get_0()	const
		{
			return _data[0];
		}
		byte_t get_1()	const
		{
			return _data[1];
		}
		byte_t get_2()	const
		{
			return _data[2];
		}

		void set_0(byte_t _ch)
		{
			_data[0] = _ch;
		}

		void set_1(byte_t _ch)
		{
			_data[1] = _ch;
		}

		void set_2(byte_t _ch)
		{
			_data[2] = _ch;
		}

		// 0000 0000	1111 1111	2222 2222
		// xxxx xxxx	xxxx xxxx	xxxx xxxx
		// 0000 0011	1111 2222	2233 3333

		int b64_0()	const	{return (_data[0] & _1111_1100) >> 2;}
		int b64_1()	const	{return ((_data[0] & _0000_0011) << 4) + ((_data[1] & _1111_0000)>>4);}
		int b64_2()	const	{return ((_data[1] & _0000_1111) << 2) + ((_data[2] & _1100_0000)>>6);}
		int b64_3()	const	{return (_data[2] & _0011_1111);}

		void b64_0(int _ch)	{_data[0] = ((_ch & _0011_1111) << 2) | (_0000_0011 & _data[0]);}

		void b64_1(int _ch)	{
			_data[0] = ((_ch & _0011_0000) >> 4) | (_1111_1100 & _data[0]);
			_data[1] = ((_ch & _0000_1111) << 4) | (_0000_1111 & _data[1]);	}

		void b64_2(int _ch)	{
			_data[1] = ((_ch & _0011_1100) >> 2) | (_1111_0000 & _data[1]);
			_data[2] = ((_ch & _0000_0011) << 6) | (_0011_1111 & _data[2]);	}

		void b64_3(int _ch){
			_data[2] = (_ch & _0011_1111) | (_1100_0000 & _data[2]);}

	private:
		byte_t _data[3];

	};




	template<class _II, class _OI, class _State, class _Endline>
		_II put(_II _First, _II _Last, _OI _To, _State& _St, _Endline _Endl)	const
	{
		three2four _3to4;
		int line_octets = 0;

		while(_First != _Last)
		{
			_3to4.zero();

			// 
			_3to4.set_0(*_First);
			_First++;

			if(_First == _Last)
			{
				*_To = std::char_traits<char>::to_char_type(_base64Chars[_3to4.b64_0()]); ++_To;
				*_To = std::char_traits<char>::to_char_type(_base64Chars[_3to4.b64_1()]); ++_To;
				*_To = std::char_traits<char>::to_char_type('='); ++_To;
				*_To = std::char_traits<char>::to_char_type('='); ++_To;
				goto __end;
			}

			_3to4.set_1(*_First);
			_First++;

			if(_First == _Last)
			{
				*_To = std::char_traits<char>::to_char_type(_base64Chars[_3to4.b64_0()]); ++_To;
				*_To = std::char_traits<char>::to_char_type(_base64Chars[_3to4.b64_1()]); ++_To;
				*_To = std::char_traits<char>::to_char_type(_base64Chars[_3to4.b64_2()]); ++_To;
				*_To = std::char_traits<char>::to_char_type('='); ++_To;
				goto __end;
			}

			_3to4.set_2(*_First);
			_First++;

			*_To = std::char_traits<char>::to_char_type(_base64Chars[_3to4.b64_0()]); ++_To;
			*_To = std::char_traits<char>::to_char_type(_base64Chars[_3to4.b64_1()]); ++_To;
			*_To = std::char_traits<char>::to_char_type(_base64Chars[_3to4.b64_2()]); ++_To;
			*_To = std::char_traits<char>::to_char_type(_base64Chars[_3to4.b64_3()]); ++_To;

			if(line_octets == 17) // base64 
			{
				//_To = _Endl(_To);
				*_To = '\n'; ++_To;
				line_octets = 0;
			}
			else
				++line_octets;
		}

		__end: ;

		return (_First);

	}


	template<class _II, class _OI, class _State>
		_II get(_II _First, _II _Last, _OI _To, _State& _St) const
	{
		three2four _3to4;
		int _Char;

		while(_First != _Last)
		{

			// Take octet
			_3to4.zero();

			// -- 0 --
			// Search next valid char... 
			while((_Char =	_getCharType(*_First)) < 0 && _Char == _UNKNOWN_CHAR)
			{
				if(++_First == _Last)
				{
					_St |= _IOS_FAILBIT|_IOS_EOFBIT; return _First; // unexpected EOF
				}
			}

			if(_Char == _EQUAL_CHAR){
				// Error! First character in octet can't be '='
				_St |= _IOS_FAILBIT; 
				return _First; 
			}
			else
				_3to4.b64_0(_Char);


			// -- 1 --
			// Search next valid char... 
			while(++_First != _Last)
				if((_Char = _getCharType(*_First)) != _UNKNOWN_CHAR)
					break;

			if(_First == _Last)	{
				_St |= _IOS_FAILBIT|_IOS_EOFBIT; // unexpected EOF 
				return _First;
			}

			if(_Char == _EQUAL_CHAR){
				// Error! Second character in octet can't be '='
				_St |= _IOS_FAILBIT; 
				return _First; 
			}
			else
				_3to4.b64_1(_Char);


			// -- 2 --
			// Search next valid char... 
			while(++_First != _Last)
				if((_Char = _getCharType(*_First)) != _UNKNOWN_CHAR)
					break;

			if(_First == _Last)	{
				// Error! Unexpected EOF. Must be '=' or base64 character
				_St |= _IOS_FAILBIT|_IOS_EOFBIT; 
				return _First; 
			}

			if(_Char == _EQUAL_CHAR){
				// OK!
				_3to4.b64_2(0); 
				_3to4.b64_3(0); 

				// chek for EOF
				if(++_First == _Last)
				{
					// Error! Unexpected EOF. Must be '='. Ignore it.
					//_St |= _IOS_BADBIT|_IOS_EOFBIT;
					_St |= _IOS_EOFBIT;
				}
				else 
					if(_getCharType(*_First) != _EQUAL_CHAR)
					{
						// Error! Must be '='. Ignore it.
						//_St |= _IOS_BADBIT;
					}
				else
					++_First; // Skip '='

				// write 1 byte to output
				*_To = (byte_t) _3to4.get_0();
				return _First;
			}
			else
				_3to4.b64_2(_Char);


			// -- 3 --
			// Search next valid char... 
			while(++_First != _Last)
				if((_Char = _getCharType(*_First)) != _UNKNOWN_CHAR)
					break;

			if(_First == _Last)	{
				// Unexpected EOF. It's error. But ignore it.
				//_St |= _IOS_FAILBIT|_IOS_EOFBIT; 
					_St |= _IOS_EOFBIT; 
				
				return _First; 
			}

			if(_Char == _EQUAL_CHAR)
			{
				// OK!
				_3to4.b64_3(0); 

				// write to output 2 bytes
				*_To = (byte_t) _3to4.get_0();
				*_To = (byte_t) _3to4.get_1();

				++_First; // set position to next character

				return _First;
			}
			else
				_3to4.b64_3(_Char);


			// write to output 3 bytes
			*_To = (byte_t) _3to4.get_0();
			*_To = (byte_t) _3to4.get_1();
			*_To = (byte_t) _3to4.get_2();

			++_First;
			

		} // while(_First != _Last)

		return (_First);
	}

protected:
	
	int _getCharType(int _Ch) const
	{
		if(_base64Chars[62] == _Ch)
			return 62;

		if(_base64Chars[63] == _Ch)
			return 63;

		if((_base64Chars[0] <= _Ch) && (_base64Chars[25] >= _Ch))
			return _Ch - _base64Chars[0];

		if((_base64Chars[26] <= _Ch) && (_base64Chars[51] >= _Ch))
			return _Ch - _base64Chars[26] + 26;

		if((_base64Chars[52] <= _Ch) && (_base64Chars[61] >= _Ch))
			return _Ch - _base64Chars[52] + 52;

		if(_Ch == std::char_traits<char>::to_int_type('='))
			return _EQUAL_CHAR;

		return _UNKNOWN_CHAR;
	}
};


void XmlBinary::fromXml(kstr &s)
{
	kstr valueEnd = strchr(s, '<');
	if (valueEnd == NULL)
		throw XmlException("Bad base64");

	data = new BinaryData();
	// check whether base64 encodings can contain chars xml encodes...

	// convert from base64 to binary
	int iostatus = 0;
	base64 decoder;
	std::back_insert_iterator<BinaryData> ins = std::back_inserter(*data);
	decoder.get(s, valueEnd, ins, iostatus);

	s = valueEnd;
}


void XmlBinary::toXmlInternal(std::ostringstream &ostr) const
{
	// convert to base64
	std::vector<char> base64data;
	int iostatus = 0;
	base64 encoder;
	std::back_insert_iterator<std::vector<char> > ins = std::back_inserter(base64data);
	encoder.put(data->begin(), data->end(), ins, iostatus, base64::crlf());

	// Wrap with xml
	ostr.write(&base64data[0], base64data.size());
}




//---------------------- Primitive Types: ---------------------

void XmlBool::fromXml(kstr &s)
{
	if (*s == '0' and s[1] == '<') {
		b = false;
		s++;
	}
	else if (*s == '1' and s[1] == '<') {
		b = true;
		s++;
	}
	else throw XmlException("bad BOOL");
}


void XmlBool::toXmlInternal(std::ostringstream &ostr) const 
{ 
	ostr << (b?"1":"0"); 
}


void XmlInt::fromXml(kstr &s)
{
	char* valueEnd;
	long ivalue = strtol(s, &valueEnd, 10);
	if (valueEnd == s)
		throw XmlException("Bad int");
	i = int(ivalue);
	s = valueEnd;
}


void XmlInt::toXmlInternal(std::ostringstream &ostr) const 
{ 
	ostr << i; 
}


void XmlDouble::fromXml(kstr &s)
{
	char* valueEnd;
	f = strtod(s, &valueEnd);
	if (valueEnd == s)
		throw XmlException("Bad double");
	s = valueEnd;
}


void XmlDouble::toXmlInternal(std::ostringstream &ostr) const 
{ 
	ostr << f;
	// This will use the default ostream::precision() to display the double.  To display
	// values with greater accuracy, call e.g.  'ostr.precision(12)' at the top level.
}


void XmlString::fromXml(kstr &s)
{
	kstr valueEnd = strchr(s, '<');
	if (valueEnd == NULL)
		throw XmlException("Bad string");
	stri = xmlDecode(s, valueEnd);
	s = valueEnd;
}


void XmlString::toXmlInternal(std::ostringstream &ostr) const 
{ 
	if (stri == NULL or *stri == '\0')
		;
	else if (strstr(stri, "]]>"))
		ostr << "CDATA elements not allowed";
	else if (SuitableForCDATA(stri))
		ostr << "\n<![CDATA[" << stri << "]]>\n";
	else ostr << xmlEncode(stri);
}


void XmlDateTime::fromXml(const char* &s)
{
	const char* valueEnd = strchr(s, '<');
	if (valueEnd == NULL)
		throw XmlException("Bad time value");
	if (sscanf_s(s, "%4d%2d%2dT%2d:%2d:%2d", &data.tm_year,&data.tm_mon,&data.tm_mday,&data.tm_hour,&data.tm_min,&data.tm_sec) != 6)
		throw XmlException("Bad time value");
	data.tm_isdst = 0;
	data.tm_year -= 1900;
	data.tm_mon -= 1;
	s = valueEnd;
}


void XmlString::asTmStruct(struct tm *data)
{
	if (sscanf_s(stri, "%4d%2d%2dT%2d:%2d:%2d", &data->tm_year,&data->tm_mon,&data->tm_mday,&data->tm_hour,&data->tm_min,&data->tm_sec) != 6)
		throw XmlException("Bad time value");
	data->tm_isdst = 0;
	data->tm_mon -= 1;
	data->tm_year -= 1900;
	data->tm_wday = data->tm_yday = 0;
}


void XmlDateTime::toXmlInternal(std::ostringstream &ostr) const
{
	char buf[30];
	_snprintf_s(buf, sizeof(buf), sizeof(buf)-1, 
				"%4d%02d%02dT%02d:%02d:%02d", 
				data.tm_year+1900,data.tm_mon+1,data.tm_mday,data.tm_hour,data.tm_min,data.tm_sec);
	ostr << buf;
}



//------------------------ RPC calls: -------------------

XmlValue* parseMethodResponse(kstr s)
{	char xmlVersion[128];

	GobbleTag(s, xmlVersion);
	if (! strbegins(xmlVersion, "<?xml version", false))
		throw XmlException(std::string(s));
	while (strchr(" \n\r\t", *s))
		s++;
	if (*s != '<')
		throw XmlException(std::string(s));			// Typically:  "java.lang.NullPointerException" + stack-trace
	return ValueFromXml(s);
}


void buildCall(kstr method, XmlArray *args, std::ostringstream &ostr)
{
	ostr << "<?xml version=\"1.0\"?>\r\n";
	ostr << '<' << method << ">\n";
	args->toXml(ostr);
	ostr << "</" << method << ">\n";
}




/*-------------------------- class XmlClient: ----------------------*/

class XmlImplementation {
	XmlClient::protocol_enum protocol;
	bool ignoreCertificateAuthority;
	HINTERNET hInternet;
	HINTERNET hConnect;
	std::string object;
	DWORD HttpErrcode;
	FILE *debugFile;
	int port;

	void hadError(str function);
	bool connect(const char* server);

public:
	struct BasicAuth_node {
		getBasicAuth_UsernameAndPassword_fn FindUsernameAndPassword;
		char username[256];
		char password[256];

		BasicAuth_node() { 
			FindUsernameAndPassword = NULL;
			username[0] = '\0';
			password[0] = '\0';
		}
	} BasicAuth;
	XmlCallback Callback;
	void *context;
	int totalBytes;
	std::string errmsg;
	bool isFault;

	XmlImplementation(const char* server, int port, const char* object, XmlClient::protocol_enum protocol);
	XmlImplementation(const char* URI, FILE *debugFile);
	XmlValue* execute(const char* method, XmlArray *params);
	void setCallback(XmlCallback Callback, void* context)
			{ this->Callback = Callback; this->context = context; }
	void setIgnoreCertificateAuthority(bool value) { ignoreCertificateAuthority = value; }
	void setDebugFile(FILE *_debugFile) { debugFile = _debugFile; }
	~XmlImplementation();
};


XmlClient::XmlClient(const char* server, int port, const char* object, protocol_enum protocol)
{
	secret = new XmlImplementation(server, port, object, protocol);
}


XmlClient::XmlClient(const char* URI, FILE *debugFile)
{
	secret = new XmlImplementation(URI, debugFile);
}


XmlValue* XmlClient::execute(const char* method, XmlArray *params)
{
	return secret->execute(method, params);
}


std::string XmlClient::getError()
{
	if (secret->errmsg.size() > 1254)
		secret->errmsg.resize(1254);
	return secret->errmsg;
}


void XmlClient::setError(std::string msg)
{
	secret->errmsg = msg;
}


void XmlClient::setCallback(XmlCallback Callback, void* context)
{
	secret->setCallback(Callback, context);
}


void XmlClient::setBasicAuth_Callback(getBasicAuth_UsernameAndPassword_fn fn)
{
	secret->BasicAuth.FindUsernameAndPassword = fn;
}


void XmlClient::setBasicAuth_UsernameAndPassword(const char* username, const char* password)
{
	strcpy(secret->BasicAuth.username, username);
	strcpy(secret->BasicAuth.password, password);
}


void XmlClient::setIgnoreCertificateAuthority(bool value)
{
	secret->setIgnoreCertificateAuthority(value);
}


void XmlClient::setDebugFile(FILE *debugFile)
{
	secret->setDebugFile(debugFile);
}


bool XmlClient::isFault() const
{
	return secret->isFault;
}


void XmlClient::close()
{
	delete secret;
	secret = NULL;
}


XmlImplementation::XmlImplementation(const char* server, int _port, const char* _object, 
												XmlClient::protocol_enum _protocol)
{
	port = _port;
	object = _object;
	if (_protocol == XmlClient::XMLRPC_AUTO)
		protocol =	(port == 80) ? XmlClient::XMLRPC_HTTP : 
					(port == 443) ? XmlClient::XMLRPC_HTTPS : XmlClient::XMLRPC_HTTP;
	else protocol = _protocol;
	ignoreCertificateAuthority = false;
	hConnect = NULL;
	debugFile = NULL;
	connect(server);
}


XmlImplementation::XmlImplementation(const char* URI, FILE* debugFile)
{
	port = 0;
	ignoreCertificateAuthority = false;
	if (strbegins(URI, "https://", false)) {
		protocol = XmlClient::XMLRPC_HTTPS;
		URI += 8;
		port = 443;
	}
	else if (strbegins(URI, "http://", false)) {
		protocol = XmlClient::XMLRPC_HTTP;
		URI += 7;
		port = 80;
	}
	else {
		errmsg = "The URI must begin with \"https://\" or \"http://\".";
		return;
	}
	kstr t = URI;
	while (*t != ':' and *t != '\0' and *t != '/')
		t++;
	std::string server(URI, t - URI);
	if (*t == ':') {
		t++;
		port = atoi(t);
		while (*t >= '0' and *t <= '9')
			t++;
	}
	object = t;		// should start with '/'.
	this->debugFile = debugFile;
	connect(server.c_str());
}


bool XmlImplementation::connect(const char* server)
{
	Callback = NULL;
	context = NULL;
	totalBytes = 0;
	hInternet = InternetOpen("Xml", 0, NULL, NULL, 0);
	if (hInternet == NULL) {
		hadError("InternetOpen");
		return false;
	}
	if (debugFile) {
		fprintf(debugFile, "Attempting to connect to server:  %s   port: %d\n", server, port);
		fflush(debugFile);
	}
	hConnect = InternetConnect(hInternet, server, port, 
					NULL, NULL, INTERNET_SERVICE_HTTP, 0, (DWORD_PTR)this);
	if (hConnect == NULL) {
		hadError("InternetConnect");
		return false;
	}
	DWORD dwTimeout=999000;		// 999 seconds
	InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(DWORD)); 
	InternetSetOption(hConnect, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof(DWORD));
	if (debugFile) {
		fputs("Got a handle\n", debugFile);
		fflush(debugFile);
	}
	return true;
}


// Converts a GetLastError() code into a human-readable string.
void XmlImplementation::hadError(str function)
{   
	errmsg = function;
	errmsg += " : ";

	int LastError = GetLastError();
	if (LastError == ERROR_INTERNET_TIMEOUT)
		errmsg += "Internet timeout";
	else if (LastError == ERROR_INTERNET_INVALID_CA)
		errmsg += "Invalid certificate authority";
	else if (LastError == ERROR_INTERNET_SECURITY_CHANNEL_ERROR)
		errmsg += "Talking HTTPS to an HTTP server?";
	else if (LastError == ERROR_INTERNET_CANNOT_CONNECT)
		errmsg += "Failed to connect";
	else if (LastError == ERROR_INTERNET_NAME_NOT_RESOLVED)
		errmsg += "Name not resolved";
	else if (LastError == ERROR_INTERNET_INVALID_URL)
		errmsg += "Invalid URL";
	else if (LastError == ERROR_INTERNET_NAME_NOT_RESOLVED)
		errmsg += "Domain name not resolved";
	else if (LastError == ERROR_INTERNET_CONNECTION_RESET)
		errmsg += "Connection reset";
	else if (LastError == ERROR_INTERNET_NOT_INITIALIZED)
		errmsg += "Internet not initialised";
	else if (LastError == ERROR_INTERNET_CONNECTION_ABORTED)
		errmsg += "Connection aborted";
	else {
		static str buf;
    	FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				LastError,
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
				(str)&buf,
				0,
				NULL
		);
		if (buf == NULL) {
			char tmp[512];
			sprintf(tmp, "Error %d", LastError);
			errmsg += tmp;
		}
		else {
			errmsg += buf;
		    LocalFree(buf);
		}
	}
	if (debugFile) {
		fprintf(debugFile, "Error: %s\n", errmsg.c_str());
		fflush(debugFile);
	}
}


static void CALLBACK myInternetCallback(HINTERNET hInternet,
				DWORD_PTR dwContext,
				DWORD dwInternetStatus,
				LPVOID lpvStatusInformation,
				DWORD dwStatusInformationLength)
{
	XmlImplementation *connection = (XmlImplementation*)dwContext;
	if (connection and connection->Callback) {
		static char buf[512];
		str status;
		switch (dwInternetStatus) {
			case INTERNET_STATUS_RECEIVING_RESPONSE:	if (connection->totalBytes == 0)
															status = "Waiting for response"; 
														else status = "Receiving response";
														break;
			case INTERNET_STATUS_RESPONSE_RECEIVED:		status = "Response received"; break;
			case INTERNET_STATUS_HANDLE_CLOSING:		status = "Handle closing"; break;
			case INTERNET_STATUS_REQUEST_SENT:			status = "Data sent"; break;
			case INTERNET_STATUS_SENDING_REQUEST:		status = "Sending data"; break;
			default:									status = buf; 
														sprintf(buf, "Status %d", dwInternetStatus);
														break;
		}
		connection->Callback(connection->context, status);
	}
}


XmlValue* XmlImplementation::execute(const char* method, XmlArray *params)
{
	errmsg = "";

	if (hConnect == NULL) {
		errmsg = "No connection";
		return false;
	}

	// Build the request as an XML file:
	std::ostringstream ostr;
	buildCall(method, params, ostr);

	// Create the HttpOpenRequest object:
	if (debugFile) {
		fputs("Sending data\n", debugFile);
		fflush(debugFile);
	}
	if (Callback)
		Callback(context, "Sending data");
	const char* acceptTypes[2] = { "text/*", NULL };
	int flags = INTERNET_FLAG_DONT_CACHE;
	flags |= INTERNET_FLAG_KEEP_CONNECTION;
	if (protocol != XmlClient::XMLRPC_HTTP)
		flags |= INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID;
RETRY:
	HINTERNET hHttpFile = HttpOpenRequest(
					  hConnect,
					  "POST",
					  object.c_str(),
					  HTTP_VERSION,
					  NULL,
					  acceptTypes,
					  flags, 
					  (DWORD_PTR)this);
	if (hHttpFile == NULL) {
		hadError("HttpOpenRequest");
		return false;
	}
	if (debugFile) {
		fputs("HttpOpenRequest() succeeded\n", debugFile);
		fflush(debugFile);
	}
	InternetSetStatusCallback(hHttpFile, myInternetCallback);

	if (ignoreCertificateAuthority) {
		DWORD dwFlags;
		DWORD dwBuffLen = sizeof(dwFlags);

		InternetQueryOption(hHttpFile, INTERNET_OPTION_SECURITY_FLAGS,
					(LPVOID)&dwFlags, &dwBuffLen);
		dwFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
		InternetSetOption(hHttpFile, INTERNET_OPTION_SECURITY_FLAGS,
							&dwFlags, sizeof (dwFlags) );
	}

	// Add the 'Content-Type' and 'Content-length' headers
	char header[255];		// Thanks, Anthony Chan.
	sprintf(header, "Content-Type: text/xml\r\nContent-length: %d", ostr.str().size());
    HttpAddRequestHeaders(hHttpFile, header, strlen(header), HTTP_ADDREQ_FLAG_ADD);

	// Add authentication fields:
	//InternetSetOption(hHttpFile, ???INTERNET_OPTION_SECURITY_FLAGS,
		//					&dwFlags, ??sizeof (dwFlags) );

	//
	std::string text = ostr.str();

	// Debug output:
	if (debugFile) {
		fputs(text.c_str(), debugFile);
		fflush(debugFile);
	}

	// Send the request:
	if (! HttpSendRequest(hHttpFile, NULL, 0, (LPVOID)text.c_str(), text.size())) {
		hadError("HttpSendRequest");
		return false;
	}
	if (Callback)
		Callback(context, "Data sent...");
	if (debugFile) {
		fputs("Waiting for response\n", debugFile);
		fflush(debugFile);
	}

	// Read the response:
	char* buf = NULL;
	int len = 0;
	do {
		DWORD bytesAvailable;
		if (!InternetQueryDataAvailable(hHttpFile, &bytesAvailable, 0, (DWORD_PTR)this)) {
			hadError("InternetQueryDataAvailable");
			break;
		}
		if (bytesAvailable == 0)
			break;		// This is the EOF condition.

		buf = (char*)realloc(buf, len+bytesAvailable+1);

		// Read the data from the HINTERNET handle.
		DWORD bytesRead;
		if (!InternetReadFile(hHttpFile,
							 (LPVOID)(buf + len),
							 bytesAvailable,
							 &bytesRead))
		{
			hadError("InternetReadFile");
			break;
		}

		len += bytesRead;
		buf[len] = '\0';
		totalBytes = len;

	} while (true);

	//
	if (debugFile) {
		fputs("Got response\n", debugFile);
		fputs(buf, debugFile);
		fflush(debugFile);
	}

	// Roel Vanhout's insertion:  Did we get a HTTP_STATUS_OK response?  If not, why not?
	// XMLRPC spec says always return 200 for a valid answer. So if it's anything other than
 	// 200, it's an error (i.e., no support for redirects etc.).
 	DWORD buf_size, dummy;
  	buf_size = sizeof(DWORD);
 	if (!HttpQueryInfo(hHttpFile, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &HttpErrcode, &buf_size, 0)) {
 		errmsg = "Could not query HTTP result status";
		if (debugFile) {
			fputs("Could not query HTTP result status\n", debugFile);
			fflush(debugFile);
		}
 		free(buf);
 		return false;
 	}
  	if (HttpErrcode == HTTP_STATUS_OK) {
		// good.
	}
  	else if (HttpErrcode == HTTP_STATUS_DENIED or HttpErrcode == HTTP_STATUS_PROXY_AUTH_REQ) {
		if (BasicAuth.FindUsernameAndPassword) {
			free(buf);
			buf = NULL;
			if (BasicAuth.FindUsernameAndPassword(BasicAuth.username[0] != '\0', BasicAuth.username, BasicAuth.password))
				goto RETRY;
		}
		errmsg = "HTTP Basic authentication failed. You need to provide a username and password.";
		goto ABORT;
	}
	else {
 		buf_size = 0;
 		HttpQueryInfo(hHttpFile, HTTP_QUERY_STATUS_TEXT, &dummy, &buf_size, 0);
 		buf_size++;	// For the '\0'
 		char* status_text = (char*)::malloc(buf_size);
 		if (! HttpQueryInfo(hHttpFile, HTTP_QUERY_STATUS_TEXT, status_text, &buf_size, 0))
 			errmsg = "Could not query HTTP result status";
		else {
			char buf[512];
			sprintf(buf, "Low level (HTTP) error: %d %s", HttpErrcode, status_text);
			errmsg = buf;
		}
		if (debugFile) {
			fprintf(debugFile, "Got status=%d\n%s\n", HttpErrcode, errmsg.c_str());
			fflush(debugFile);
		}
ABORT:
	    InternetCloseHandle(hHttpFile);
 		free(buf);
 		return false;
 	}
 
	// Close the HttpRequest object:
    InternetCloseHandle(hHttpFile);

	// Parse the response:
	if (len == 0) {
		free(buf);		// 'buf' will always be NULL unless for some strange reason,
						// InternetReadFile() returns 'false' on the first pass.
		errmsg = "The server responded with an empty message.";
		return false;
	}

	XmlValue *result = NULL;
	try {
		result = parseMethodResponse(buf);
	}
	catch (XmlException err) {
		errmsg = err.getMessage();
	}
	free(buf);

	// Finished:
	return result;
}


XmlImplementation::~XmlImplementation()
{
	if (hConnect)
		InternetCloseHandle(hConnect);
	if (hInternet)
		InternetCloseHandle(hInternet);
}


}; // namespace PlainXML



//---------------------------- Unit testing: ----------------------

#if 0
using PlainXML::XmlValue;


static void RoundTrip(XmlValue *a)
{
	std::ostringstream ostr;
	a->toXml(ostr);
	std::string buf = ostr.str();
	XmlBool *b = ValueFromXml(buf.c_str());
	//assert(a == b);
}


void XmlUnitTest()
{
	try {
		XmlValue result;
		char buf[327680];
		std::ifstream input("C:\\Documents and Settings\\edval\\Desktop\\Edval data\\debug.txt");
		input.read(buf, sizeof(buf));
		kstr s = buf;
		result.fromXml(s);
		for (int i=0; i < result.size(); i++) {
			XmlValue el = result[i];
			std::string _AcademicYear = el["ACADEMIC_YEAR"];
			int AcademicYearId = el["ACADEMIC_YEAR_ID"];
		}
	} catch (PlainXML::XmlException e) {
	}

	std::vector<XmlValue> V;
	V.push_back(10);
	V.push_back(20);
	V.push_back(30);
	V.push_back(40);
	V.push_back(50);

	XmlValue a = "Hello you";
	RoundTrip(a);

	XmlValue harry;
	harry[0] = 56;
	harry[1] = 560;
	harry[2] = 115;
	harry[3] = 145;
	harry[4] = 35;

	//5
	XmlValue binny((void*)XmlUnitTest, 30);
	RoundTrip(binny);

	XmlValue jenny;
	jenny["NAME"] = "Electric boots";
	jenny["code"] = "54121";
	jenny["status"] = true;

	//14
	a.clear();
	a["CHARGE"] = 505;
	a["SPIN"] = "foo";
	a["COLOUR"] = false;
	a["BENNY"] = harry;
	a["BINNY"] = binny;
	a["JENNY"] = jenny;
	a["EMPTY"] = "";
	RoundTrip(a);

	// Copy constructors:
	XmlValue b;
	{
		XmlValue copy = a;
		XmlValue tmp = copy;
		b = tmp;
	}
	assert(a == b);
}

#endif
