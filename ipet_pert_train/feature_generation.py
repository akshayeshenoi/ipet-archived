import pandas as pd
import numpy as np
from constants import total_time, omega, device_name
from multiprocessing import Pool
import os 

pd.options.mode.chained_assignment = None

def check_polarity(ip_src):
    if ip_src.startswith('192'):
        return 'O'
    else:
        return 'I'

def get_one_feature(feat_mini):
    sub_temp = feat_mini.copy()
    sub_temp.drop(['rel_time','feature_num'],axis=1,inplace = True)
    sub_temp = sub_temp[['packet_polarity','count','tcp_len','udp_len']]    
    feature = None
    sub_temp = sub_temp.groupby('packet_polarity').sum()
    if 'O' in sub_temp.index and 'I' in sub_temp.index:
        feature = sub_temp.loc['I'].tolist()+sub_temp.loc['O'].tolist()
    elif 'O' not in sub_temp.index:
        feature = sub_temp.loc['I'].tolist()+[0,0,0]
    else:
        feature = [0,0,0]+sub_temp.loc['O'].tolist()
    feature = np.array(feature)
    feature =  pd.Series(feature, copy=False)
    return feature

def get_feature_vector(df,max_size,sample_size):
    feature = list()
    temp = df.copy()
    start_time = temp.iloc[0].rel_time
    temp['rel_time'] = temp['rel_time'].apply(lambda x:x-start_time)
    temp['feature_num'] = temp['rel_time'].apply(lambda x:x//omega)
    temp['count'] = 1
    missing = [i for i in range(int(total_time/omega)) if i not in temp['feature_num'].unique()]
    dictionary_list = []
    for val in missing:
        dictionary_data = {'feature_num':val,'rel_time':0,'packet_polarity':'I','tcp_len':0,'udp_len':0,'count':0}
        dictionary_list.append(dictionary_data)
    temp = temp.append(dictionary_list)
    ans = temp.groupby(temp['feature_num']).apply(get_one_feature)
    ans = ans.to_numpy()
    return ans

def getFeatures_multi(experiment_tuple):
    (device,total_time,omega) = experiment_tuple
    features_main, silences,labels = list(),list(),list()

    file_name = 'IoT_Traces/'+str(device)+'.csv'
    full = pd.read_csv(file_name)
    full['packet_polarity'] = full['ip.src'].apply(lambda x: check_polarity(x))
    full['tcp.len'].replace(-1,0,inplace = True)
    full['udp.length'].replace(-1,0,inplace = True)
    full = full[['frame.time_relative','tcp.len','udp.length','packet_polarity']]
    full.columns = ['rel_time','tcp_len','udp_len','packet_polarity']
    rel_time = list(full['rel_time'])
    if len(rel_time)>0:
        time_prev = rel_time[0]
        silence = 0
        time_start = rel_time[0]
        time_obs_curr = 0
        for time_now in rel_time:
            time_obs_curr = time_now - time_start
            silence = time_now - time_prev
            if time_obs_curr>=total_time:
                feat_help = full[(full['rel_time']>=time_start) & (full['rel_time']<time_start+total_time)]
                if feat_help.shape[0]>0:
                    features_main.append(get_feature_vector(feat_help,total_time,omega))
                    silences.append(silence)
                time_start = time_now
                time_prev = time_now
            else:
                time_prev = time_now
                
    features_main,silences = np.array(features_main), np.array(silences)
    labels = np.ones(features_main.shape[0])*actual_id[device]
    np.save('Features/features_'+str(device)+'.npy',features_main)
    np.save('Features/silences_'+str(device)+'.npy',silences)
    np.save('Features/labels_'+str(device)+'.npy',labels)

keys = device_name
values = [i for i in range(len(device_name))]
actual_id = dict(zip(keys,values))

experiments_all = []
for device in device_name:
    file_name = 'IoT_Traces/'+str(device)+'.csv'
    if os.path.isfile(file_name):
        getFeatures_multi((device,total_time,omega))
        experiments_all.append(device)        

device = experiments_all[0]
combined_main_features = np.load('Features/features_'+str(device)+'.npy')
combined_silences = np.load('Features/silences_'+str(device)+'.npy')
combined_labels = np.load('Features/labels_'+str(device)+'.npy')

for device in experiments_all[1:]:
    features_main = np.load('Features/features_'+str(device)+'.npy')
    silences = np.load('Features/silences_'+str(device)+'.npy')
    labels = np.load('Features/labels_'+str(device)+'.npy')

    if features_main.shape[0]>0:
        combined_main_features = np.concatenate((combined_main_features,features_main))
        combined_silences = np.concatenate((combined_silences,silences))
        combined_labels = np.concatenate((combined_labels,labels))

idx_new = np.random.permutation(combined_main_features.shape[0])
combined_main_features,combined_silences,combined_labels = combined_main_features[idx_new], combined_silences[idx_new], combined_labels[idx_new]

np.save('Features/new_features_main.npy',combined_main_features)
np.save('Features/new_silences.npy',combined_silences)
np.save('Features/new_labels.npy',combined_labels)