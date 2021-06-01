#include "../core/logger.h"

#include "../core/datetime.h"

using namespace std;

Logger::Logger()
  :logToStdout(false),logToStderr(false),logTime(true),ostreams(),files()
{}

Logger::~Logger()
{
  for(size_t i = 0; i<logBufs.size(); i++)
    delete logBufs[i];

  for(size_t i = 0; i<files.size(); i++) {
    files[i]->close();
    delete files[i];
  }
}

bool Logger::isLoggingToStdout() const {
  return logToStdout;
}

bool Logger::isLoggingToStderr() const {
  return logToStderr;
}

void Logger::setLogToStdout(bool b) {
  logToStdout = b;
}
void Logger::setLogToStderr(bool b) {
  logToStderr = b;
}
void Logger::setLogTime(bool b) {
  logTime = b;
}
void Logger::addOStream(ostream& out) {
  ostreams.push_back(&out);
}
void Logger::addFile(const string& file) {
  if(file != "")
    files.push_back(new ofstream(file, ofstream::app));
}

void Logger::write(const string& str, bool endLine) {
  lock_guard<mutex> lock(mutex);
  time_t time = DateTime::getNow();
  const char* timeFormat = "%F %T%z: ";

  if(logToStdout) {
    if(logTime) { DateTime::writeTimeToStream(cout, timeFormat, time); cout << str; }
    else cout << ": " << str;
    if(endLine) cout << endl; else cout << flush;
  }
  if(logToStderr) {
    if(logTime) { DateTime::writeTimeToStream(cerr, timeFormat, time); cerr << str; }
    else cerr << ": " << str;
    if(endLine) cerr << endl; else cerr << flush;
  }
  for(size_t i = 0; i<ostreams.size(); i++) {
    ostream& out = *(ostreams[i]);
    if(logTime) { DateTime::writeTimeToStream(out, timeFormat, time); out << str; }
    else out << ": " << str;
    if(endLine) out << endl; else out << flush;
  }
  for(size_t i = 0; i<files.size(); i++) {
    ofstream& out = *(files[i]);
    if(logTime) { DateTime::writeTimeToStream(out, timeFormat, time); out << str; }
    else out << ": " << str;
    if(endLine) out << endl; else out << flush;
  }
}

void Logger::write(const string& str) {
  write(str,true);
}

void Logger::writeNoEndline(const string& str) {
  write(str,false);
}

ostream* Logger::createOStream() {
  unique_lock<mutex> lock(mutex);
  LogBuf* logBuf = new LogBuf(this);
  logBufs.push_back(logBuf);
  lock.unlock();
  return new ostream(logBuf);
}

LogBuf::LogBuf(Logger* l)
  :stringbuf(),logger(l)
{}

LogBuf::~LogBuf()
{}

int LogBuf::sync() {
  const string& str = this->str();
  logger->writeNoEndline(str);
  this->str("");
  return 0;
}

void Logger::logThreadUncaught(const string& name, Logger* logger, function<void()> f) {
  try {
    f();
  }
  catch(const exception& e) {
    if(logger != NULL)
      logger->write(string("ERROR: " + name + " loop thread failed: ") + e.what());
    else
      cerr << (string("ERROR: " + name + " loop thread failed: " )+ e.what()) << endl;
    this_thread::sleep_for(chrono::duration<double>(5.0));
    throw;
  }
  catch(...) {
    if(logger != NULL)
      logger->write("ERROR: " + name + " loop thread failed with unexpected throw");
    else
      cerr << "ERROR: " + name + " loop thread failed with unexpected throw" << endl;
    this_thread::sleep_for(chrono::duration<double>(5.0));
    throw;
  }
}
