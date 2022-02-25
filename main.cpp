#include <maya/MFnPlugin.h>
#include <maya/MAnimControl.h>
#include <maya/MDGContextGuard.h>
#include <maya/MViewport2Renderer.h>
#include <maya/MSelectionList.h>
#include <maya/MFrameContext.h>
#include <maya/MPointArray.h>
#include <maya/MColorArray.h>
#include <maya/MAnimMessage.h>
#include <maya/MFnDagNode.h>
#include <maya/MPxCommand.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MSyntax.h>
#include <maya/MDagPath.h>
#include <maya/M3dView.h>
#include <maya/MGlobal.h>

#include <vector>
#include <map>
using PointCache = std::map<int, std::pair<MPoint, MColor> >;


// render operation for tracking/drawing the trajectories

class SimpleRenderOperation : public MHWRender::MHUDRender
{
public:
  static int frames;
  static double pointSize;
  static bool screenSpace;
  static std::vector<std::pair<MDagPath, PointCache> > trackData;

public:
  SimpleRenderOperation(const MString &name): MHUDRender(){}
  virtual ~SimpleRenderOperation(){}
  virtual MStatus execute(const MHWRender::MDrawContext& drawContext)
  {
    return MStatus::kSuccess;
  }
  virtual bool hasUIDrawables() const {return true;}
  virtual void addUIDrawables(
    MHWRender::MUIDrawManager& drawManager,
    const MHWRender::MFrameContext& frameContext
  )
  {
    if (trackData.size() == 0)
    {
      return;
    }

    MDagPath dag_cam = frameContext.getCurrentCameraPath();
    MMatrix mat_cam = dag_cam.inclusiveMatrix();
    std::map<int, MMatrix> M_ss;

    int t_current = MAnimControl::currentTime().value();
    int t_start = std::max(t_current - frames, int(MAnimControl::minTime().value()));
    int t_end = std::min(t_current + frames + 1, int(MAnimControl::maxTime().value()));

    MPointArray P;
    MColorArray C;

    for (auto& track : trackData)
    {
      auto& dag = track.first;
      auto& cache = track.second;

      // take draw override color from the object if set
      MColor cc;
      if (!MFnDagNode(track.first).drawOverrideColor(cc))
      {
        cc = MColor(1, 0.65, 0.25, 1);
      }

      for (int tt = t_start; tt < t_end; ++tt)
      {
        // cache the frame in its context if not available
        if (cache.find(tt) == cache.end())
        {
          MDGContextGuard ctx(MDGContext(MTime(tt, MTime::uiUnit())));
          auto m = track.first.inclusiveMatrix();
          
          auto p = MPoint(m[3][0], m[3][1], m[3][2]);
          if (screenSpace)
          {
            if (M_ss.find(tt) == M_ss.end())
            {
              M_ss[tt] = dag_cam.inclusiveMatrix().inverse() * mat_cam;
            }
            p *= M_ss[tt];
          }

          // project to viewport
          MPoint q;
          frameContext.worldToViewport(p, q.x, q.y);

          cache[tt] = {q, cc};
        }
        P.append(cache[tt].first);

        // apply smoothstep transparency so the trajectory fades out at both ends
        double x = 1.0 - std::fabs(double(tt - t_current)) / frames;
        x *= x;
        C.append(MColor(cc.r, cc.g, cc.b, 3 * x - 2 * x));
      }
    }

    // draw points on viewport
    drawManager.beginDrawable();
    auto radius = 0.5 * pointSize;
    for (unsigned int ii = 0; ii < P.length(); ++ii)
    {
      drawManager.setColor(C[ii]);
      drawManager.circle2d(P[ii], radius, true);
    }
    drawManager.endDrawable();
  }
};

int SimpleRenderOperation::frames = 24;
double SimpleRenderOperation::pointSize = 5;
bool SimpleRenderOperation::screenSpace = true;
std::vector<std::pair<MDagPath, PointCache> > SimpleRenderOperation::trackData;

// render override to add the render operation

class SimpleRenderOverride : public MHWRender::MRenderOverride
{
protected:
  SimpleRenderOperation* mSimpleRenderOp;
  MString mUIName = "Simple Render Override";

public:
  ~SimpleRenderOverride() override {}
  SimpleRenderOverride(const MString& name): 
    MHWRender::MRenderOverride(name)
  {
    MHWRender::MRenderer *theRenderer = MHWRender::MRenderer::theRenderer();
    if (!theRenderer)
    {
      return;
    }
    // append the render op at the end of we draw tracks on top of the existing scene
    MHWRender::MRenderer::theRenderer()->getStandardViewportOperations(mOperations);
    mSimpleRenderOp = new SimpleRenderOperation("simpleRenderOp");
    mOperations.insertAfter(MHWRender::MRenderOperation::kStandardSceneName, mSimpleRenderOp);
  }
  MHWRender::DrawAPI supportedDrawAPIs() const override {return MHWRender::kAllDevices;}
  MString uiName() const override {return mUIName;}
  static MString viewName;
};

MString SimpleRenderOverride::viewName = "simpleTrackView";

// simple command interface for setting up tracking objects

class simpleTrackCmd : public MPxCommand
{
public:
  MStatus doIt(const MArgList& args) override;
  static void* creator() {return new simpleTrackCmd;}
  static MSyntax newSyntax()
  {
    MSyntax syntax;
    syntax.useSelectionAsDefault(true);
    syntax.setObjectType(MSyntax::kSelectionList);
    syntax.addFlag("cl", "clear", MSyntax::kUnsigned);
    syntax.addFlag("nf", "numOfFrames", MSyntax::kUnsigned);
    syntax.addFlag("ps", "pointSize", MSyntax::kDouble);
    syntax.addFlag("ss", "screenSpace", MSyntax::kUnsigned);
    return syntax;
  }

  static void clearTracking()
  {
    SimpleRenderOperation::trackData.clear();
    MMessage::removeCallback(cbidAnimEdited);
  }

  // could be smarter but for now just clear for all if any one is modified
  static void OnAnimKeyframeEdited(MObjectArray& deltaObjs, void*)
  {
    for (auto& track : SimpleRenderOperation::trackData)
    {
      track.second.clear();
    }
    M3dView().refresh(true, true);
  }

public:
  static MCallbackId cbidAnimEdited;
};

MCallbackId simpleTrackCmd::cbidAnimEdited;

MStatus simpleTrackCmd::doIt(const MArgList& args)
{
  MStatus status = MStatus::kSuccess;
  MArgDatabase argData(syntax(), args, &status);

  if (argData.isFlagSet("clear"))
  {
    clearTracking();
  }
  else // obtain the selected dags
  {
    clearTracking();
    MSelectionList sl;
    argData.getObjects(sl);
    for (unsigned int ii = 0; ii < sl.length(); ++ii)
    {
      MDagPath dag;
      if (MStatus::kSuccess == sl.getDagPath(ii, dag))
      {
        SimpleRenderOperation::trackData.push_back({dag, PointCache()});
      }
    }
    if (SimpleRenderOperation::trackData.size())
    {
      cbidAnimEdited = MAnimMessage::addAnimKeyframeEditedCallback(OnAnimKeyframeEdited);
    }
  }

  if (argData.isFlagSet("screenSpace"))
  {
    SimpleRenderOperation::screenSpace = argData.flagArgumentBool("screenSpace", 0);
  }
  if (argData.isFlagSet("numOfFrames"))
  {
    SimpleRenderOperation::frames = argData.flagArgumentInt("numOfFrames", 0);
  }
  if (argData.isFlagSet("pointSize"))
  {
    SimpleRenderOperation::pointSize = argData.flagArgumentDouble("pointSize", 0);
  }
  // force viewport to update
  MGlobal::executeCommandOnIdle("refresh;");

  return status;
}

// plugin registration/deregistration

MStatus initializePlugin(MObject obj)
{
  MStatus status = MStatus::kSuccess;
  MFnPlugin plugin(obj, "Simple Render Example", "Any", "Any");

  MHWRender::MRenderer* renderer = MHWRender::MRenderer::theRenderer();
  if (renderer)
  {
    SimpleRenderOverride *overridePtr = 
      new SimpleRenderOverride(SimpleRenderOverride::viewName);
    if (overridePtr)
    {
      renderer->registerOverride(overridePtr);
    }
  }

  status = plugin.registerCommand(
    "simpleTrack",
    simpleTrackCmd::creator,
    simpleTrackCmd::newSyntax
  );
  if (!status)
  {
      status.perror("Failed to register simpleTrack");
  }
  
  return status;
}

MStatus uninitializePlugin(MObject obj)
{
  MStatus status = MStatus::kSuccess;
  MFnPlugin plugin(obj);

  MHWRender::MRenderer* renderer = MHWRender::MRenderer::theRenderer();
  if (renderer)
  {
    const MHWRender::MRenderOverride* overridePtr = 
      renderer->findRenderOverride(SimpleRenderOverride::viewName);
    if (overridePtr)
    {
      renderer->deregisterOverride(overridePtr);
      delete overridePtr;
    }
  }

  simpleTrackCmd::clearTracking();
  status = plugin.deregisterCommand("simpleTrack");
  if (!status)
  {
    status.perror("Failed to deregister simpleTrack");
  }

  return status;
}
