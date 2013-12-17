
#include <memory>

/* Using the PyCXX API for C++ Python integration
 * It is extremely convenient and avoids the need to write boilerplate
 * code for handling the Python reference counts.
 * It is licensed under BSD terms compatible with reSIProcate */
#include <Python.h>
#include <CXX/Objects.hxx>

#include "rutil/Logger.hxx"
#include "resip/stack/Helper.hxx"
#include "repro/Plugin.hxx"
#include "repro/Processor.hxx"
#include "repro/RequestContext.hxx"

#define RESIPROCATE_SUBSYSTEM resip::Subsystem::REPRO

using namespace resip;
using namespace repro;

static PyMethodDef PyRouteMethods[] = {
  {NULL, NULL, 0, NULL}
};

class PyRoutePlugin : public Plugin, public Processor
{
   public:
      PyRoutePlugin() : Processor("PyRoute") {};
      ~PyRoutePlugin(){};

      virtual bool init(ProxyConfig *proxyConfig)
      {
         DebugLog(<<"PyRoutePlugin: init called");

         if(!proxyConfig)
         {
            ErrLog(<<"proxyConfig == 0, aborting");
            return false;
         }

         Data pyPath(proxyConfig->getConfigData("PyRoutePath", "", true));
         Data routeScript(proxyConfig->getConfigData("PyRouteScript", "", true));
         if(pyPath.empty())
         {
            ErrLog(<<"PyRoutePath not specified in config, aborting");
            return false;
         }
         if(routeScript.empty())
         {
            ErrLog(<<"PyRouteScript not specified in config, aborting");
            return false;
         }

         // FIXME: what if there are other Python modules?
         Py_Initialize();
         Py_InitModule("pyroute", PyRouteMethods);
         PyObject *sys_path = PySys_GetObject("path");
         PyObject *addpath = PyString_FromString(pyPath.c_str());
         PyList_Append(sys_path, addpath);
         PyEval_InitThreads();

         PyObject *pyModule = PyImport_ImportModule(routeScript.c_str());
         if(!pyModule)
         {
            ErrLog(<<"Failed to load module "<< routeScript);
            return false;
         }
         mPyModule.reset(new Py::Module(pyModule));

         if(mPyModule->getDict().hasKey("on_load"))
         {
            StackLog(<< "found on_load method, trying to invoke it...");
            try
            {
               mPyModule->callMemberFunction("on_load");
            }
            catch (const Py::Exception& ex)
            {
               DebugLog(<< "call to on_load method failed: " << Py::value(ex));
               StackLog(<< Py::trace(ex));
               return false;
            }
         } 

         mAction = mPyModule->getAttr("provide_route");

         return true;
      }

      virtual void onRequestProcessorChainPopulated(ProcessorChain& chain)
      {
         DebugLog(<<"PyRoutePlugin: onRequestProcessorChainPopulated called");

         // The module class is also the monkey class, no need to create
         // any monkey instance here

         // Add the pyroute monkey to the chain
         chain.addProcessor(std::auto_ptr<Processor>(this));
      }

      virtual void onResponseProcessorChainPopulated(ProcessorChain& chain)
      {
         DebugLog(<<"PyRoutePlugin: onResponseProcessorChainPopulated called");
      }

      virtual void onTargetProcessorChainPopulated(ProcessorChain& chain)
      {
         DebugLog(<<"PyRoutePlugin: onTargetProcessorChainPopulated called");
      }

      /*
       * Now we implemented the Processor API from repro/Processor.hxx
       */

      virtual processor_action_t process(RequestContext &context)
      {
         DebugLog(<< "Monkey handling request: PyRoute");

         SipMessage& msg = context.getOriginalRequest();
         if(msg.method() != INVITE && msg.method() != MESSAGE)
         {
            // We only route INVITE and MESSAGE, otherwise we ignore
            return Processor::Continue;
         }
         Py::String reqUri(msg.header(h_RequestLine).uri().toString().c_str());
         Py::Tuple args(1);
         args[0] = reqUri;
         Py::List routes;
         try
         {
            routes = mAction.apply(args);
         }
         catch (const Py::Exception& ex)
         {
            DebugLog(<< Py::value(ex));
            StackLog(<< Py::trace(ex));
            context.sendResponse(*std::auto_ptr<SipMessage>
               (Helper::makeResponse(msg, 500, "Server Internal Error")));
            return SkipAllChains;
         }
         DebugLog(<< "got " << routes.size() << " result(s).");
         for(
            Py::Sequence::iterator i = routes.begin();
            i != routes.end();
            i++)
         {
            Py::String target(*i);
            Data target_s(target.as_std_string());
            DebugLog(<< "processing result: " << target_s);
            context.getResponseContext().addTarget(NameAddr(target_s));
         }
         
         return Processor::Continue;
      }

   private:
      std::auto_ptr<Py::Module> mPyModule;
      Py::Callable mAction;
};


extern "C" {

static
Plugin* instantiate()
{
   return new PyRoutePlugin();
}

ReproPluginDescriptor reproPluginDesc =
{
   REPRO_DSO_PLUGIN_API_VERSION,
   &instantiate
};

};

/* ====================================================================
 *
 * Copyright 2013 Daniel Pocock http://danielpocock.com  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of the author(s) nor the names of any contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * ====================================================================
 *
 *
 */
