import unittest
from cg_appearance_policy import analyze

class AppearanceTest(unittest.TestCase):
    def policy(self,steps):
        program=dict(resources=['cg','other'],commands=[],keys=[],events=[],event_lines={})
        doc=dict(path='test',events=[])
        for i,(op,commands,params,indent) in enumerate(steps):
            doc['events'].append(dict(op=op,line=i+1,indent=indent,params=params))
            first=len(program['commands']);program['commands'].extend(commands)
            program['event_lines'][str(i+1)]=i;program['events'].append([first,len(commands),i+1])
        flags,report=analyze(doc,program,{'cg'});self.assertTrue(report['converged'])
        return flags,report
    def test_blur_later_rejects_entire_first_appearance_but_not_second(self):
        flags,_=self.policy([show(),step([set_(6,256)],'SpawnBlur'),step([set_(6,0)],'DestroySpawnedExtended'),hide(),show()])
        self.assertFalse(flags[0][3]);self.assertTrue(flags[4][3])
    def test_repeated_image_command_does_not_split_visible_appearance(self):
        flags,_=self.policy([show(),show(),step([set_(6,256)],'SpawnBlur'),hide()])
        self.assertFalse(flags[0][3]);self.assertFalse(flags[1][3])
    def test_hidden_blur_does_not_poison_visible_appearances(self):
        flags,_=self.policy([show(),hide(),step([set_(6,256)],'SpawnBlur'),step([set_(6,0)]),show()])
        self.assertTrue(flags[0][3]);self.assertTrue(flags[4][3])
    def test_replacement_ends_previous_appearance(self):
        flags,_=self.policy([show(),step([[1,0,0,0,3,0,0]]),step([set_(6,256)],'SpawnBlur')])
        self.assertTrue(flags[0][3])
    def test_show_actors_starts_a_new_appearance_without_replacing_image(self):
        flags,_=self.policy([show(),step([set_(6,256)],'SpawnBlur'),hide(),step([set_(6,0)]),step([set_(4,256)],'ShowActors')])
        self.assertFalse(flags[0][3]);self.assertTrue(flags[4][3])
    def test_any_conditional_blur_rejects_the_whole_appearance(self):
        flags,_=self.policy([show(),('BeginIf',[],{'Expression':'unknown'},0),step([set_(6,256)],'SpawnBlur',1),('Else',[],{},0),step([],'Wait',1),hide()])
        self.assertFalse(flags[0][3])

def set_(prop,value):return [value,0,0,9,3,prop,0]
def step(commands,op='ModifyBackgroundExtended',indent=0):return op,commands,{},indent
def show():return step([[0,0,0,0,3,0,0],set_(4,256)])
def hide():return step([set_(4,0)],'HideActors')
if __name__=='__main__':unittest.main()
