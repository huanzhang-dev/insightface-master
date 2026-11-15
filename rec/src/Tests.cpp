#include <cmath>
#include <stdexcept>
#include "Car.hpp"
#include "Multilingual.hpp"
#include <iostream>
#include <stdexcept>
namespace car {
static void require(bool test,const char* message){if(!test)throw std::runtime_error(message);}
static int runLightingTests(const std::string& models) {
 int checks=0;Lighting correction;cv::Mat black(120,160,CV_8UC3,cv::Scalar());require(cv::norm(correction.apply(black),black,cv::NORM_INF)==0,"Black signal must not be amplified");++checks;
 cv::Mat day(120,160,CV_8UC3,cv::Scalar(180,180,180));require(cv::norm(correction.apply(day),day,cv::NORM_INF)==0,"Daylight passthrough");++checks;
 cv::Mat dark(120,160,CV_8UC3,cv::Scalar(12,12,12));cv::rectangle(dark,cv::Rect(30,30,60,40),cv::Scalar(32,32,32),cv::FILLED);cv::Mat original=dark.clone(),enhanced=correction.apply(dark);require(cv::mean(enhanced)[0]>cv::mean(dark)[0]&&cv::norm(dark,original,cv::NORM_INF)==0,"Dark enhancement must preserve original pixels");++checks;
 require(nightExposureView(black).empty()&&nightExposureView(day).empty()&&!nightExposureView(dark).empty(),"Secondary exposure gating");++checks;
 cv::Mat flat(40,140,CV_8UC1,cv::Scalar(8));require(cv::norm(normalizePlateLighting(flat),flat,cv::NORM_INF)==0,"Flat plate noise guard");++checks;
 {std::string expected="\xe4\xba\xac\x41\x31\x32\x33\x34\x35";MultilingualOcr ocr(models+"/tessdata","chi_sim",false);cv::Mat patch=renderTextLine(expected,400,80,"Microsoft YaHei",false);patch.convertTo(patch,CV_8U,.18,2);cv::Mat normalized=normalizePlateLighting(patch);Reading text=ocr.read(normalized);std::cout<<"Dark chi_sim fixture -> "<<text.text<<" confidence="<<text.confidence<<'\n';require(text.text==expected,"Dark chi_sim OCR failed");++checks;}
 {std::string expected="\x31\x32\xea\xb0\x80\x33\x34\x35\x36";MultilingualOcr ocr(models+"/tessdata","kor",false);cv::Mat patch=renderTextLine(expected,400,80,"Malgun Gothic",false);patch.convertTo(patch,CV_8U,.18,2);cv::Mat normalized=normalizePlateLighting(patch);Reading text=ocr.read(normalized);std::cout<<"Dark kor fixture -> "<<text.text<<" confidence="<<text.confidence<<'\n';require(text.text==expected,"Dark kor OCR failed");++checks;}
 {std::string expected="\xe5\x93\x81\xe5\xb7\x9d\x31\x32\x33\x34";MultilingualOcr ocr(models+"/tessdata","jpn",false);cv::Mat patch=renderTextLine(expected,400,80,"Yu Gothic",false);patch.convertTo(patch,CV_8U,.18,2);cv::Mat normalized=normalizePlateLighting(patch);Reading text=ocr.read(normalized);std::cout<<"Dark jpn fixture -> "<<text.text<<" confidence="<<text.confidence<<'\n';require(text.text==expected,"Dark jpn OCR failed");++checks;}
 {std::string expected="\x41\x42\x31\x32\x33\x34";MultilingualOcr ocr(models+"/tessdata","eng",false);cv::Mat patch=renderTextLine(expected,400,80,"Arial",false);patch.convertTo(patch,CV_8U,.18,2);cv::Mat normalized=normalizePlateLighting(patch);Reading text=ocr.read(normalized);std::cout<<"Dark eng fixture -> "<<text.text<<" confidence="<<text.confidence<<'\n';require(text.text==expected,"Dark eng OCR failed");++checks;}
 return checks;
}
static int runReviewTests(){
 int tests=0;Detection d;d.box=cv::Rect2f(10,10,60,40);d.plateBox=cv::Rect(20,30,40,12);d.plateDetected=true;d.plateSource="detector";d.plate.text="\xe4\xba\xac" "A12345";d.plate.confidence=.54f;
 Tracker chinese;for(int i=0;i<3;++i){chinese.predict(i*.1);chinese.update({d},i*.1);}require(chinese.tracks()[0].plateStable&&chinese.tracks()[0].plateVisible,"Evidence-backed Chinese consensus at moderate confidence");++tests;
 Tracker unsupported;Detection unsupportedText=d;unsupportedText.plateDetected=false;for(int i=0;i<4;++i){unsupported.predict(i*.1);unsupported.update({unsupportedText},i*.1);}require(unsupported.tracks()[0].plate.text.empty(),"OCR without plate detection must not identify a plate");++tests;
 Tracker one;one.predict(0);one.update({d},0);require(!one.tracks()[0].plateStable,"One moderate-confidence OCR read is not confirmation");++tests;
 one.update({d},0);one.update({d},0);require(!one.tracks()[0].plateStable,"Duplicate observation timestamp must not establish consensus");++tests;
 Detection hidden=d;hidden.plate=Reading();hidden.plateDetected=false;chinese.predict(.3);chinese.update({hidden},.3);require(!chinese.tracks()[0].plateVisible&&plateStatus(chinese.tracks()[0])=="historical","Hidden plate must be historical, never a current read");++tests;
 Tracker observed;for(int i=0;i<3;++i){observed.predict(i*.1);observed.update({d},i*.1);}require(!observed.project(.8).tracks()[0].plateVisible&&observed.project(3).tracks().empty(),"Projected view must age plate visibility and expire tracks");++tests;
 Detection conflicting=d;conflicting.plate.text="XY1234";observed.predict(.3);observed.update({conflicting},.3);observed.update({conflicting},.3);require(!observed.tracks()[0].plateVisible&&observed.tracks()[0].lastPlateSeen==.2&&observed.tracks()[0].plateCounts.at("XY1234")==1,"Conflicting or duplicate reads must not refresh historical evidence timestamps");++tests;
 Tracker moving;Detection vehicle;vehicle.box=cv::Rect2f(10,10,50,30);for(int i=0;i<10;++i){moving.predict(i*.1);vehicle.box.x=10+20*i*.1f;moving.update({vehicle},i*.1);}float measured=moving.tracks()[0].box.x;Tracker projected=moving.project(1.2);require(projected.tracks()[0].box.x>measured+1&&moving.tracks()[0].box.x==measured,"Delayed measurement projection must advance position without mutating measurement-time state");++tests;
 for(int size:std::vector<int>{1,8,32,59,60,80}){cv::Mat tiny(size,size,CV_8UC3,cv::Scalar());draw(tiny,chinese,30);}++tests;
 require(!Options().allowTextCandidates,"Production default must prohibit heuristic text candidates");++tests;
 return tests;
}
static int runIdentityTests(){
 int count=0;require(typeName(1)=="bicycle"&&typeName(2)=="passenger_car"&&typeName(3)=="motorcycle"&&typeName(5)=="bus"&&typeName(7)=="truck","Supported wheeled road vehicle labels");++count;cv::Mat patch(80,120,CV_8UC3,cv::Scalar(20,50,180));cv::rectangle(patch,cv::Rect(15,10,60,25),cv::Scalar(40,40,40),cv::FILLED);auto feature=appearanceFeature(patch);require(feature.size()==128&&feature==appearanceFeature(patch),"Deterministic vehicle appearance feature");++count;
 Detection a,b;a.box=cv::Rect2f(20,20,80,50);a.appearance={1,0};b.box=cv::Rect2f(300,20,100,60);b.appearance={0,1};b.category=5;Tracker tracker;tracker.predict(0);tracker.update({a,b},0);int first=tracker.tracks()[0].id,second=tracker.tracks()[1].id;
 tracker.predict(3.);require(tracker.tracks().empty()&&tracker.archivedTracks().size()==2,"Lost vehicles retain archived identities without ghost active boxes");++count;
 tracker.update({b,a},3.);require(tracker.tracks().size()==2&&tracker.tracks()[0].id==first&&tracker.tracks()[1].id==second&&tracker.tracks()[0].reacquisitions==1,"Returning vehicles recover original IDs despite reversed detections");++count;
 tracker.predict(20.);tracker.update({a,b},20.);require(tracker.tracks()[0].id==first&&tracker.tracks()[1].id==second&&tracker.tracks()[0].reacquisitions==2,"Original IDs survive repeated long observation gaps");++count;
 tracker.predict(24.);Detection other=a;other.appearance={.7071f,.7071f};tracker.update({other},24.);require(tracker.tracks()[0].id>second,"Unrelated appearance must not steal an archived ID");++count;
 require(tracker.archivedTracks().size()==2&&tracker.project(30.).tracks().empty(),"Lost identity archive persists without claiming current visibility");++count;
 return count;
}
static int runAssociationFixTests(){
 int count=0;Detection car;car.box=cv::Rect2f(20,20,80,50);car.appearance={1,0};car.confidence=.9f;car.category=2;
 Tracker fast;fast.predict(0);fast.update({car},0);int original=fast.tracks()[0].id;Detection moved=car;moved.box.x=160;fast.predict(.3);fast.update({moved},.3);require(fast.tracks().size()==1&&fast.tracks()[0].id==original&&fast.tracks()[0].box.x>150,"Fast motion must recover the active ID instead of allocating a duplicate");++count;
 Tracker weak;weak.predict(0);weak.update({car},0);Detection uncertain=moved;uncertain.appearance={.94f,.3412f};weak.predict(.3);weak.update({uncertain},.3);require(weak.tracks().size()==2&&weak.tracks().back().id!=weak.tracks().front().id,"Weak appearance must not bypass the motion gate");++count;
 Tracker activeClass;activeClass.predict(0);activeClass.update({car},0);Detection bus=moved;bus.category=5;activeClass.predict(.3);activeClass.update({bus},.3);require(activeClass.tracks().size()==2,"Active appearance fallback must not merge car and bus");++count;
 Tracker archiveClass;archiveClass.predict(0);archiveClass.update({car},0);int archivedId=archiveClass.tracks()[0].id;archiveClass.predict(3.);bus.box=car.box;archiveClass.update({bus},3.);require(archiveClass.tracks()[0].id!=archivedId&&archiveClass.archivedTracks().size()==1&&archiveClass.archivedTracks()[0].category==2,"Bus must not inherit archived car ID even with identical appearance");++count;
 Tracker ambiguous;Detection second=car;second.box.x=300;ambiguous.predict(0);ambiguous.update({car,second},0);ambiguous.predict(.3);ambiguous.update({moved},.3);require(ambiguous.tracks().size()==3&&ambiguous.tracks().back().id>2,"Ambiguous appearance must not arbitrarily select either old ID");++count;
 Tracker missing;Detection noFeature=car;noFeature.appearance.clear();missing.predict(0);missing.update({noFeature},0);noFeature.box.x=160;missing.predict(.3);missing.update({noFeature},.3);require(missing.tracks().size()==2,"Missing appearance must not justify fast-motion identity recovery");++count;
 IdSwitchCounter alternating;for(int id:std::vector<int>{1,2,1})alternating.observe(id);require(alternating.switches==2,"A to B to A must count two ID changes");++count;
 IdSwitchCounter gaps;for(int id:std::vector<int>{1,0,1,0,2,2,-1})gaps.observe(id);require(gaps.switches==1&&gaps.previousId==2,"Missing observations and repeated IDs must not count as switches");++count;
 return count;
}
int runTests(const std::string& models){int tests=runLightingTests(models)+runReviewTests()+runIdentityTests()+runAssociationFixTests();require(letters("AB123CD")=="ABCD"&&numbers("AB123CD")=="123","Character separation");++tests;require(letters("123456").empty()&&numbers("123456")=="123456","Numeric-only separation");++tests;auto assignment=assign({{.9,.1},{.2,.8}});require(assignment[0]==1&&assignment[1]==0,"Global assignment");++tests;
 Tracker tracker;Detection a,b;a.box=cv::Rect2f(20,30,80,60);a.plate.text="AB1234";a.plate.confidence=.95f;a.plateDetected=true;a.plateBox=cv::Rect(30,60,50,15);a.plateSource="detector";b.box=cv::Rect2f(400,30,90,70);b.category=5;b.plate.text="789012";b.plate.confidence=.95f;b.plateDetected=true;b.plateBox=cv::Rect(410,60,50,15);b.plateSource="detector";tracker.predict(0);tracker.update({a,b},0);int first=tracker.tracks()[0].id,second=tracker.tracks()[1].id;tracker.predict(.1);a.box.x+=5;b.box.x-=5;tracker.update({b,a},.1);require(tracker.tracks()[0].id==first&&tracker.tracks()[1].id==second,"IDs persist with reordered detections");require(tracker.tracks()[0].plateStable&&tracker.tracks()[0].plate.text=="AB1234","Temporal plate consensus");tests+=2;tracker.predict(.5);require(tracker.tracks().size()==2,"Short occlusion survival");++tests;tracker.predict(3);require(tracker.tracks().empty(),"Expired tracks removed");tracker.update({a},3);require(tracker.tracks()[0].id>second,"IDs never reused");tests+=2;
 Recognizer reader(models+"/ocr_latin.onnx");for(const std::string& text:std::vector<std::string>{"AB1234","789012"}){cv::Mat patch=renderTextLine(text,240,64);Reading result=reader.read(patch);std::cout<<"OCR fixture "<<text<<" -> "<<result.text<<" confidence="<<result.confidence<<'\n';require(result.text==text,"OCR fixture failed");++tests;}
 Reading small=reader.read(renderTextLine("AB1234",136,32));std::cout<<"Small plate -> "<<small.text<<" confidence="<<small.confidence<<'\n';require(small.text=="AB1234","Small plate OCR failed");++tests;
 
 std::string mixed="\xe7\xb2\xa4\xea\xb0\x80\xd0\x96\xc3\xa9\xd9\xa1\xef\xbc\x92\x39\xf0\x9d\x9f\xa0\x21";
 require(characters(mixed)=="\xe7\xb2\xa4\xea\xb0\x80\xd0\x96\xc3\xa9\x21"&&numbers(mixed)=="\xd9\xa1\xef\xbc\x92\x39\xf0\x9d\x9f\xa0","Unicode character/number separation");++tests;
 require(textLength(mixed)==9,"Unicode scalar count including supplementary character");++tests;
 require(compactText(" A\t12\n")=="A12","OCR whitespace compaction");++tests;
 bool rejected=false;try{textSymbols(std::string("\xff"));}catch(const std::exception&){rejected=true;}require(rejected,"Invalid UTF-8 rejected");++tests;
 require(jsonText("a\"\n")=="\"a\\\"\\u000a\""&&csvText("a\"b")=="\"a\"\"b\"","JSON and CSV escaping");++tests;
 MultilingualOcr chinese(models+"/tessdata","chi_sim",false);
 std::string chineseText="\xe4\xba\xac\x41\x31\x32\x33\x34\x35";Reading unicodeReading=chinese.read(renderTextLine(chineseText,400,80,"Microsoft YaHei",false));
 std::cout<<"Chinese OCR fixture -> "<<unicodeReading.text<<" confidence="<<unicodeReading.confidence<<'\n';
 cv::imwrite(models+"/../testdata/plate_unicode.png",renderTextLine(chineseText,400,80,"Microsoft YaHei",false));
 require(unicodeReading.text==chineseText,"Chinese OCR fixture failed");++tests;
 MultilingualOcr russian(models+"/tessdata","rus",false);
 std::string russianText="\xd0\x96\x31\x32\x33\x34\x35";Reading russianReading=russian.read(renderTextLine(russianText,400,80,"Arial",false));
 std::cout<<"Cyrillic OCR fixture -> "<<russianReading.text<<" confidence="<<russianReading.confidence<<'\n';
 require(russianReading.text==russianText,"Cyrillic OCR fixture failed");++tests;
 cv::imwrite(models+"/../testdata/plate_unicode.png",renderTextLine(chineseText,400,80,"Microsoft YaHei",false));

 { MultilingualOcr scriptReader(models+"/tessdata","kor",false);std::string expected="\x31\x32\xea\xb0\x80\x33\x34\x35\x36";cv::Mat fixture=renderTextLine(expected,400,80,"Malgun Gothic",false);Reading result=scriptReader.read(fixture);cv::imwrite(models+"/../testdata/plate_kor.png",fixture);std::cout<<"Korean OCR fixture -> "<<result.text<<" confidence="<<result.confidence<<'\n';require(result.text==expected,"Korean OCR fixture failed");++tests;}
 { MultilingualOcr scriptReader(models+"/tessdata","jpn",false);std::string expected="\xe5\x93\x81\xe5\xb7\x9d\x31\x32\x33\x34";cv::Mat fixture=renderTextLine(expected,400,80,"Yu Gothic",false);Reading result=scriptReader.read(fixture);cv::imwrite(models+"/../testdata/plate_jpn.png",fixture);std::cout<<"Japanese OCR fixture -> "<<result.text<<" confidence="<<result.confidence<<'\n';require(result.text==expected,"Japanese OCR fixture failed");++tests;}

 Options options;options.models=models;Pipeline pipeline(options);cv::Mat blank(480,640,CV_8UC3,cv::Scalar(100,100,100));require(pipeline.detect(blank).empty(),"Blank scene should not create a vehicle");++tests;std::cout<<"PASS "<<tests<<" checks\n";return 0;
}
}
